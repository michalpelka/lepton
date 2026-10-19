package com.lepton.cdcviewer

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.hardware.usb.UsbRequest
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.TimeoutException

/**
 * Minimal USB CDC-ACM client for the Pico, using Android's USB host API
 * directly (no /dev/ttyACM access without root).
 *
 * A CDC-ACM device has two interfaces: a "communications" one (class 0x02)
 * that takes control requests, and a "data" one (class 0x0A) with the bulk
 * IN/OUT endpoints that carry the actual byte stream.
 */
class PicoUsb private constructor(
    private val connection: UsbDeviceConnection,
    private val controlInterface: UsbInterface,
    private val dataInterface: UsbInterface,
    private val bulkIn: UsbEndpoint,
) : AutoCloseable {

    /**
     * Reads until [isRunning] returns false or the device goes away, passing
     * each received chunk to [sink] (buffer contents valid only during the call).
     *
     * Keeps several asynchronous requests queued so the host always has an IN
     * request pending and the Pico never waits for us between reads; a single
     * blocking bulkTransfer() leaves the bus idle while we process each chunk.
     */
    fun readLoop(isRunning: () -> Boolean, sink: (ByteBuffer, Int) -> Unit) {
        val requests = List(QUEUED_REQUESTS) {
            UsbRequest().apply {
                initialize(connection, bulkIn)
                // Direct buffer = plain native memory, readable from C++ without a copy.
                clientData = ByteBuffer.allocateDirect(READ_CHUNK)
            }
        }
        try {
            for (r in requests) r.queue(r.clientData as ByteBuffer)
            while (isRunning()) {
                val r = try {
                    connection.requestWait(200)
                } catch (e: TimeoutException) {
                    continue
                } ?: break // device gone
                val buf = r.clientData as ByteBuffer
                // On completion the position is advanced by the number of bytes received.
                if (buf.position() > 0) sink(buf, buf.position())
                buf.clear()
                if (!r.queue(buf)) break
            }
        } finally {
            for (r in requests) {
                r.cancel()
                r.close()
            }
        }
    }

    override fun close() {
        setControlLineState(connection, controlInterface, dtr = false)
        connection.releaseInterface(dataInterface)
        connection.releaseInterface(controlInterface)
        connection.close()
    }

    companion object {
        /** Raspberry Pi's USB vendor ID, also in res/xml/device_filter.xml. */
        const val PICO_VENDOR_ID = 0x2E8A

        // Max size of one USB request on older Android versions.
        private const val READ_CHUNK = 16384
        private const val QUEUED_REQUESTS = 8

        private const val USB_CLASS_CDC_DATA = 0x0A
        private const val REQUEST_TYPE_CLASS_INTERFACE_OUT = 0x21
        private const val SET_CONTROL_LINE_STATE = 0x22

        fun findPico(usb: UsbManager): UsbDevice? =
            usb.deviceList.values.firstOrNull { it.vendorId == PICO_VENDOR_ID }

        fun open(usb: UsbManager, device: UsbDevice): PicoUsb {
            var control: UsbInterface? = null
            var data: UsbInterface? = null
            var bulkIn: UsbEndpoint? = null
            for (i in 0 until device.interfaceCount) {
                val iface = device.getInterface(i)
                when (iface.interfaceClass) {
                    UsbConstants.USB_CLASS_COMM -> if (control == null) control = iface
                    USB_CLASS_CDC_DATA -> if (data == null) {
                        data = iface
                        bulkIn = (0 until iface.endpointCount).map { iface.getEndpoint(it) }.firstOrNull {
                            it.type == UsbConstants.USB_ENDPOINT_XFER_BULK && it.direction == UsbConstants.USB_DIR_IN
                        }
                    }
                }
            }
            if (control == null || data == null || bulkIn == null)
                throw IOException("${device.deviceName} has no CDC-ACM interface")

            val connection = usb.openDevice(device) ?: throw IOException("openDevice failed")
            if (!connection.claimInterface(control, true) || !connection.claimInterface(data, true)) {
                connection.close()
                throw IOException("claimInterface failed")
            }
            // The firmware only sends while tud_cdc_connected(), i.e. after the
            // host raises DTR. Desktop serial drivers do this on open(); here
            // it's on us.
            setControlLineState(connection, control, dtr = true)
            return PicoUsb(connection, control, data, bulkIn)
        }

        private fun setControlLineState(connection: UsbDeviceConnection, control: UsbInterface, dtr: Boolean) {
            val value = if (dtr) 0x03 else 0x00 // bit 0 DTR, bit 1 RTS
            connection.controlTransfer(
                REQUEST_TYPE_CLASS_INTERFACE_OUT, SET_CONTROL_LINE_STATE, value, control.id, null, 0, 1000
            )
        }
    }
}
