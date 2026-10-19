package com.lepton.cdcviewer

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var usb: UsbManager
    private lateinit var status: TextView
    private lateinit var recordButton: Button
    private val viewer = NativeViewer()
    private val ui = Handler(Looper.getMainLooper())

    private var pico: PicoUsb? = null
    private var reader: Thread? = null
    @Volatile private var reading = false
    private var recordDir: File? = null

    private var lastStats: NativeViewer.Stats? = null
    private var statusText = ""

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION ->
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) connect()
                    else setStatus("USB permission denied")
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    disconnect()
                    setStatus("Pico unplugged")
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        usb = getSystemService(Context.USB_SERVICE) as UsbManager
        status = findViewById(R.id.status)
        recordButton = findViewById(R.id.record)
        recordButton.setOnClickListener { toggleRecording() }

        val thermal = findViewById<ThermalView>(R.id.thermal)
        thermal.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = viewer.setSurface(holder.surface)
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {}
            override fun surfaceDestroyed(holder: SurfaceHolder) = viewer.setSurface(null)
        })
        // Touch replaces the desktop mouse-over temperature readout.
        thermal.setOnTouchListener { v, e ->
            if (e.action == MotionEvent.ACTION_DOWN || e.action == MotionEvent.ACTION_MOVE)
                viewer.setCursor((e.x * 640 / v.width).toInt(), (e.y * 480 / v.height).toInt())
            true
        }

        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else
            registerReceiver(usbReceiver, filter)

        ui.post(object : Runnable {
            override fun run() {
                updateStats()
                ui.postDelayed(this, 1000)
            }
        })
        connect()
    }

    // Launched/re-launched by plugging in the Pico (USB_DEVICE_ATTACHED).
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        connect()
    }

    override fun onDestroy() {
        ui.removeCallbacksAndMessages(null)
        unregisterReceiver(usbReceiver)
        disconnect()
        viewer.close()
        super.onDestroy()
    }

    private fun connect() {
        if (pico != null) return
        val device = PicoUsb.findPico(usb) ?: return setStatus("Plug in the Pico (USB OTG)")
        if (!usb.hasPermission(device)) return requestPermission(device)

        val conn = try {
            PicoUsb.open(usb, device)
        } catch (e: IOException) {
            Log.e(TAG, "open failed", e)
            return setStatus("Failed to open Pico: ${e.message}")
        }
        pico = conn
        reading = true
        reader = Thread({
            conn.readLoop({ reading }) { buf, n -> viewer.feed(buf, n) }
        }, "pico-reader").apply { start() }
        setStatus("Connected to ${device.productName ?: device.deviceName}")
    }

    private fun disconnect() {
        reading = false
        reader?.join()
        reader = null
        pico?.close()
        pico = null
        if (recordDir != null) toggleRecording()
    }

    private fun requestPermission(device: UsbDevice) {
        // The intent must be explicit (setPackage) and mutable so UsbManager can
        // attach EXTRA_PERMISSION_GRANTED to it.
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(packageName)
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        usb.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, flags))
        setStatus("Waiting for USB permission…")
    }

    private fun toggleRecording() {
        if (recordDir == null) {
            // App-specific external storage: no storage permission needed;
            // fetch with: adb pull /sdcard/Android/data/com.lepton.cdcviewer/files/
            val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val dir = File(getExternalFilesDir(null), "rec_$stamp")
            if (!viewer.startRecording(dir.absolutePath)) return setStatus("Cannot create ${dir.path}")
            recordDir = dir
            recordButton.text = "Stop"
            setStatus("Recording to ${dir.name}")
        } else {
            viewer.stopRecording()
            setStatus("Saved ${recordDir!!.name}")
            recordDir = null
            recordButton.text = "Record"
        }
    }

    private fun setStatus(text: String) {
        statusText = text
        status.text = text
    }

    // Called once a second: rates are deltas since the previous call.
    private fun updateStats() {
        val s = viewer.stats()
        val prev = lastStats
        lastStats = s
        if (pico == null || prev == null) {
            status.text = statusText
            return
        }
        val fps = s.framesShown - prev.framesShown
        val received = s.framesReceived - prev.framesReceived
        val kbps = (s.bytesReceived - prev.bytesReceived) / 1024
        val dropped = s.framesDropped - prev.framesDropped
        val line = "$fps fps shown / $received received, USB $kbps KB/s, " +
            "decode ${s.lastProcessUs / 1000} ms, dropped $dropped"
        status.text = "$statusText — $line"
        // Also to logcat, so a session can be reviewed later with adb logcat -s LeptonViewer.
        Log.i(TAG, line)
    }

    companion object {
        private const val TAG = "LeptonViewer"
        private const val ACTION_USB_PERMISSION = "com.lepton.cdcviewer.USB_PERMISSION"
    }
}
