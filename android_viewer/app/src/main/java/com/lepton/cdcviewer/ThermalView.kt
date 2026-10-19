package com.lepton.cdcviewer

import android.content.Context
import android.util.AttributeSet
import android.view.SurfaceView

/** SurfaceView that keeps the Lepton's 4:3 aspect ratio inside whatever space it gets. */
class ThermalView(context: Context, attrs: AttributeSet?) : SurfaceView(context, attrs) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val maxW = MeasureSpec.getSize(widthMeasureSpec)
        val maxH = MeasureSpec.getSize(heightMeasureSpec)
        val (w, h) = if (maxW * 3 > maxH * 4) Pair(maxH * 4 / 3, maxH) else Pair(maxW, maxW * 3 / 4)
        setMeasuredDimension(w, h)
    }
}
