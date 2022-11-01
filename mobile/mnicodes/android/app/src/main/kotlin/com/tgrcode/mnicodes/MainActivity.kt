package com.tgrcode.mnicodes

import android.content.ContentValues
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import io.flutter.embedding.engine.renderer.FlutterRenderer
import java.io.File
import java.io.IOException

import android.graphics.SurfaceTexture
import io.flutter.view.TextureRegistry

class MainActivity : FlutterActivity() {
    private lateinit var renderer: FlutterRenderer
    //private lateinit var surfaceTexture: SurfaceTexture
    //private lateinit var surfaceTextureEntry: TextureRegistry.SurfaceTextureEntry
    private var renders = HashMap<Long, MniRenderer>()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        renderer = flutterEngine.getRenderer()
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "flutter_media_store")
                .setMethodCallHandler { call, result ->
                    when (call.method) {
                        "addItem" -> {
                            addItem(
                                    call.argument("path")!!,
                                    call.argument("name")!!,
                                    call.argument("mime")!!
                            )
                            result.success(null)
                        }
                        "createTexture" -> {
                            result.success(getFlutterTexture(
                                call.argument("buffer")!!
                            ))
                        }
                        "disposeTexture" -> {
                            //result.success(getFlutterTexture(
                            //    call.argument("textureID")!!
                            //))
                        }
                    }
                }
    }

    private fun getFlutterTexture(buffer: ByteArray): Long {
        // https://github.com/mogol/opengl_texture_widget_example
        var entry = renderer.createSurfaceTexture();
        var surfaceTexture = entry.surfaceTexture();

        //int width = arguments.get("width").intValue();
        //int height = arguments.get("height").intValue();
        var width = 512;
        var height = 512;
        surfaceTexture.setDefaultBufferSize(width, height);

        var render = MniRenderer(surfaceTexture, buffer);

        renders.put(entry.id(), render);

        return entry.id();
    }

    private fun addItem(path: String, name: String, mimeType: String) {
        val collection =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                } else {
                    MediaStore.Downloads.EXTERNAL_CONTENT_URI
                }

        val values =
                ContentValues().apply {
                    put(MediaStore.Downloads.DISPLAY_NAME, name)
                    put(MediaStore.Downloads.MIME_TYPE, mimeType)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        put(
                                MediaStore.DownloadColumns.RELATIVE_PATH,
                                Environment.DIRECTORY_DOWNLOADS
                        )
                        put(MediaStore.Downloads.IS_PENDING, 1)
                    }
                }

        val resolver = applicationContext.contentResolver
        val uri = resolver.insert(collection, values)!!

        try {
            resolver.openOutputStream(uri).use { os ->
                File(path).inputStream().use { it.copyTo(os!!) }
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                values.clear()
                values.put(MediaStore.Downloads.IS_PENDING, 0)
                resolver.update(uri, values, null, null)
            }
        } catch (ex: IOException) {
            if (ex.message != null) {
                Log.e("MediaStore", ex.message.toString())
            } else {
                Log.e("MediaStore", "error")
            }
        }
    }
}
