package com.tgrcode.mnicodes

import android.graphics.SurfaceTexture
import android.opengl.GLUtils
import android.util.Log
import java.util.concurrent.atomic.AtomicInteger
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.egl.EGLSurface

class MniRenderer(texture: SurfaceTexture, buffer: ByteArray) : Runnable {
    companion object {
        private const val LOG_TAG = "MniRenderer"

        init {
            System.loadLibrary("mni_android_native")
        }

        external fun loadFromBuffer(buffer: ByteArray): Boolean
        external fun renderNextFrame(): Boolean
        external fun setRotation(angle: Int): Boolean
		external fun setPress(x: Double, y: Double): Boolean
    }

    public var rotation: Int = 0
	public var x_press: Double = -1.0;
	public var y_press: Double = -1.0;

    protected val texture: SurfaceTexture
    private lateinit var egl: EGL10
    private lateinit var eglDisplay: EGLDisplay
    private lateinit var eglContext: EGLContext
    private lateinit var eglSurface: EGLSurface
    private lateinit var buffer: ByteArray
    private var running: Boolean
	private var reload: Boolean = false

    override fun run() {
        initGL()

        // TODO on create
        loadFromBuffer(buffer)

        Log.d(LOG_TAG, "OpenGL init OK.")
        while (running) {
            val loopStart: Long = System.currentTimeMillis()

			if (reload) {
				loadFromBuffer(buffer)
				reload = false
			}

            // Send in input
            setRotation(getRotationSync())
			val (x, y) = getPressSync()
			setPress(x, y)

            if (renderNextFrame()) {
                if (!egl.eglSwapBuffers(eglDisplay, eglSurface)) {
                    Log.d(LOG_TAG, GLUtils.getEGLErrorString(egl.eglGetError()))
                }
            }
            val waitDelta: Long = 16 - (System.currentTimeMillis() - loopStart)
            if (waitDelta > 0) {
                try {
                    Thread.sleep(waitDelta)
                } catch (e: InterruptedException) {
                }
            }
        }

        // TODO on deconstruct
        //worker.onDispose()

        deinitGL()
    }

    @Synchronized
    fun setRotationSync(angle: Int) {
        rotation = angle
    }

	@Synchronized
    fun getRotationSync(): Int {
        return rotation
    }

	@Synchronized
    fun setPressSync(x: Double, y: Double) {
        x_press = x
		y_press = y
    }

	@Synchronized
    fun getPressSync(): Pair<Double, Double> {
        return Pair(x_press, y_press)
    }

	@Synchronized
    fun setBuffer(buffer: ByteArray) {
        this.buffer = buffer
    }

	@Synchronized
    fun triggerReload() {
        this.reload = true
    }

    private fun initGL() {
        egl = EGLContext.getEGL() as EGL10
        eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY)
        if (eglDisplay === EGL10.EGL_NO_DISPLAY) {
            throw RuntimeException("eglGetDisplay failed")
        }
        val version = IntArray(2)
        if (!egl.eglInitialize(eglDisplay, version)) {
            throw RuntimeException("eglInitialize failed")
        }
        val eglConfig: EGLConfig = chooseEglConfig()
        eglContext = createContext(egl, eglDisplay, eglConfig)
        eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, texture, null)
        if (eglSurface == null || eglSurface === EGL10.EGL_NO_SURFACE) {
            throw RuntimeException("GL Error: " + GLUtils.getEGLErrorString(egl.eglGetError()))
        }
        if (!egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            throw RuntimeException(
                "GL make current error: " + GLUtils.getEGLErrorString(egl.eglGetError())
            )
        }
    }

    private fun deinitGL() {
        egl.eglMakeCurrent(
            eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT
        )
        egl.eglDestroySurface(eglDisplay, eglSurface)
        egl.eglDestroyContext(eglDisplay, eglContext)
        egl.eglTerminate(eglDisplay)
        Log.d(LOG_TAG, "OpenGL deinit OK.")
    }

    private fun createContext(
        egl: EGL10,
        eglDisplay: EGLDisplay,
        eglConfig: EGLConfig
    ): EGLContext {
        val EGL_CONTEXT_CLIENT_VERSION = 0x3098
        val attribList = intArrayOf(EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE)
        return egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, attribList)
    }

    private fun chooseEglConfig(): EGLConfig {
        val configsCount = IntArray(1)
        val configs: Array<EGLConfig?> = arrayOfNulls<EGLConfig>(1)
        val configSpec = config
        if (!egl.eglChooseConfig(eglDisplay, configSpec, configs, 1, configsCount)) {
            throw IllegalArgumentException(
                "Failed to choose config: " + GLUtils.getEGLErrorString(egl.eglGetError())
            )
        }/* else if (configsCount[0] > 0) {
            return configs[0]
        }*/

        return configs[0]!!
    }

    private val config: IntArray
        private get() = intArrayOf(
            EGL10.EGL_RENDERABLE_TYPE, 4,
            EGL10.EGL_RED_SIZE, 8,
            EGL10.EGL_GREEN_SIZE, 8,
            EGL10.EGL_BLUE_SIZE, 8,
            EGL10.EGL_ALPHA_SIZE, 8,
            EGL10.EGL_DEPTH_SIZE, 16,
            EGL10.EGL_STENCIL_SIZE, 0,
            EGL10.EGL_SAMPLE_BUFFERS, 1,
            EGL10.EGL_SAMPLES, 4,
            EGL10.EGL_NONE
        )

    //@Override
    //@Throws(Throwable::class)
    //protected fun finalize() {
    //    super.finalize()
    //    running = false
    //}

    fun onDispose() {
        running = false
    }

    init {
        this.texture = texture
        this.buffer = buffer
        running = true
        val thread = Thread(this)
        thread.start()
    }
}