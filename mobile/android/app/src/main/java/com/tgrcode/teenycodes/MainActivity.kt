package com.tgrcode.teenycodes

import android.Manifest
import android.graphics.Color
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.view.SurfaceView
import android.view.ViewGroup
import android.widget.Toast
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.Lifecycle
import com.google.mlkit.vision.barcode.BarcodeScanner
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import com.tgrcode.teenycodes.databinding.ActivityMainBinding
import org.libsdl.app.SDLActivity
import java.util.concurrent.Executors


private const val CAMERA_PERMISSION_REQUEST_CODE = 1

// Based on https://github.com/bea-droid/barcodescanner
@ExperimentalGetImage
class MainActivity : SDLActivity(), LifecycleOwner {
	private lateinit var binding: ActivityMainBinding
	// For the camera
	private lateinit var lifecycleRegistry: LifecycleRegistry

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		setContentView(binding.root)

		lifecycleRegistry = LifecycleRegistry(this)
		lifecycleRegistry.currentState = Lifecycle.State.CREATED

		if (hasCameraPermission()) {
			bindCameraUseCases()
		} else {
			requestPermission()
		}
	}

	public override fun onStart() {
		super.onStart()
		lifecycleRegistry.currentState = Lifecycle.State.STARTED
	}

	override fun getLifecycle(): Lifecycle {
		return lifecycleRegistry
	}

	override fun addApplicationSurface(surface: SurfaceView): ViewGroup {
		binding = ActivityMainBinding.inflate(layoutInflater)
		//binding.cameraView.overlay.add(binding.QRLayout)
		binding.QRLayout.addView(surface)
		return binding.QRLayout
	}

	override fun getLibraries(): Array<String> {
		return arrayOf(
			"SDL2",
			"tinycode_android_native"
		)
	}

	external fun registerCode(qr: ByteArray): Boolean
	external fun getCodeName(): String

	// checking to see whether user has already granted permission
	private fun hasCameraPermission() =
		ActivityCompat.checkSelfPermission(
			this,
			Manifest.permission.CAMERA
		) == PackageManager.PERMISSION_GRANTED

	private fun requestPermission(){
		// opening up dialog to ask for camera permission
		ActivityCompat.requestPermissions(
			this,
			arrayOf(Manifest.permission.CAMERA),
			CAMERA_PERMISSION_REQUEST_CODE
		)
	}

	override fun onRequestPermissionsResult(
		requestCode: Int,
		permissions: Array<out String>,
		grantResults: IntArray
	) {
		if (requestCode == CAMERA_PERMISSION_REQUEST_CODE
			&& grantResults[0] == PackageManager.PERMISSION_GRANTED) {
			// user granted permissions - we can set up our scanner
			bindCameraUseCases()
		} else {
			// user did not grant permissions - we can't use the camera
			Toast.makeText(this,
				"Camera permission required",
				Toast.LENGTH_LONG
			).show()
		}

		super.onRequestPermissionsResult(requestCode, permissions, grantResults)
	}

	private fun bindCameraUseCases() {
		val cameraProviderFuture = ProcessCameraProvider.getInstance(this)

		cameraProviderFuture.addListener({
			val cameraProvider = cameraProviderFuture.get()

			// setting up the preview use case
			val previewUseCase = Preview.Builder()
				.build()
				.also {
					it.setSurfaceProvider(binding.cameraView.surfaceProvider)
				}

			// configure our MLKit BarcodeScanning client

			/* passing in our desired barcode formats - MLKit supports additional formats outside of the
			ones listed here, and you may not need to offer support for all of these. You should only
			specify the ones you need */
			val options = BarcodeScannerOptions.Builder().setBarcodeFormats(
				Barcode.FORMAT_QR_CODE
			).build()

			// getClient() creates a new instance of the MLKit barcode scanner with the specified options
			val scanner = BarcodeScanning.getClient(options)

			// setting up the analysis use case
			val analysisUseCase = ImageAnalysis.Builder()
				.build()

			// define the actual functionality of our analysis use case
			analysisUseCase.setAnalyzer(
				Executors.newSingleThreadExecutor(),
				{ imageProxy ->
					processImageProxy(scanner, imageProxy)
				}
			)

			// configure to use the back camera
			val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA

			try {
				cameraProvider.bindToLifecycle(
					this,
					cameraSelector,
					previewUseCase,
					analysisUseCase)
			} catch (illegalStateException: IllegalStateException) {
				// If the use case has already been bound to another lifecycle or method is not called on main thread.
				Log.e(TAG, illegalStateException.message.orEmpty())
			} catch (illegalArgumentException: IllegalArgumentException) {
				// If the provided camera selector is unable to resolve a camera to be used for the given use cases.
				Log.e(TAG, illegalArgumentException.message.orEmpty())
			}
		}, ContextCompat.getMainExecutor(this))
	}

	private fun processImageProxy(
		barcodeScanner: BarcodeScanner,
		imageProxy: ImageProxy
	) {

		imageProxy.image?.let { image ->
			val inputImage =
				InputImage.fromMediaImage(
					image,
					imageProxy.imageInfo.rotationDegrees
				)

			barcodeScanner.process(inputImage)
				.addOnSuccessListener { barcodeList ->
					val barcode = barcodeList.getOrNull(0)

					barcode?.rawBytes?.let { qr ->
						// Raw bytes of qr code
						registerCode(qr)
						binding.bottomText.text = getCodeName()
					}

					//barcode?.rawValue?.let { value ->
					//	binding.bottomText.text =
					//		getString(R.string.barcode_value, value) + " " + JniInterface().getAwesomeMessage()
					//}
				}
				.addOnFailureListener {
					// This failure will happen if the barcode scanning model
					// fails to download from Google Play Services

					Log.e(TAG, it.message.orEmpty())
				}.addOnCompleteListener {
					// When the image is from CameraX analysis use case, must
					// call image.close() on received images when finished
					// using them. Otherwise, new images may not be received
					// or the camera may stall.

					imageProxy.image?.close()
					imageProxy.close()
				}
		}
	}

	companion object {
		val TAG: String = MainActivity::class.java.simpleName
	}
}