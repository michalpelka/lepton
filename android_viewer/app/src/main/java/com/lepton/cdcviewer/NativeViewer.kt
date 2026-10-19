package com.lepton.cdcviewer

import android.view.Surface
import java.nio.ByteBuffer

/** Kotlin face of native_viewer.cpp: parsing, decoding, drawing and recording live in C++. */
class NativeViewer : AutoCloseable {
    private var handle: Long = nativeCreate()

    /** Push raw bytes received from the Pico (direct buffer). Called from the USB reader thread. */
    fun feed(data: ByteBuffer, len: Int) = nativeFeed(handle, data, len)

    fun setSurface(surface: Surface?) = nativeSetSurface(handle, surface)

    /** Crosshair position in 640x480 display coordinates, (-1, -1) hides it. */
    fun setCursor(x: Int, y: Int) = nativeSetCursor(handle, x, y)

    fun startRecording(dir: String): Boolean = nativeStartRecording(handle, dir)

    fun stopRecording() = nativeStopRecording(handle)

    class Stats(
        val bytesReceived: Long,
        val framesReceived: Long,
        val framesShown: Long,
        val framesDropped: Long,
        val lastProcessUs: Long,
    )

    fun stats(): Stats = nativeStats(handle).let { Stats(it[0], it[1], it[2], it[3], it[4]) }

    /** Only call once the reader thread has stopped feeding. */
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeFeed(handle: Long, data: ByteBuffer, len: Int)
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeSetCursor(handle: Long, x: Int, y: Int)
    private external fun nativeStartRecording(handle: Long, dir: String): Boolean
    private external fun nativeStopRecording(handle: Long)
    private external fun nativeStats(handle: Long): LongArray

    companion object {
        init {
            System.loadLibrary("lepton_viewer")
        }
    }
}
