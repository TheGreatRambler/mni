for d in */ ; do
	# If argument provided, only do that one
	if [[ $# -gt 0 ]] && [[ "$1" != "$d" ]]; then
		continue
	fi

	echo Building ${d}main.owasm
	#clang -Oz -I../mni/include --no-standard-libraries --target=wasm32 -Wl,--no-entry -Wl,--export-all -o ${d}main.wasm ${d}main.cpp
	emcc -Oz -I../mni/include -sFILESYSTEM=0 -sERROR_ON_UNDEFINED_SYMBOLS=0 --no-entry ${d}main.cpp -o ${d}main.wasm
	# Covert to optimized wasm
	echo mni compile -o ${d}main.owasm ${d}main.wasm
	../build/mni.exe compile -o ${d}main.owasm ${d}main.wasm
	# Convert to QR code
	echo mni compile -q ${d}main.png ${d}main.wasm
	../build/mni.exe compile -q ${d}main.png ${d}main.wasm
	# Start runtime
	echo mni run --wasm ${d}main.owasm
	../build/mni.exe run --wasm ${d}main.owasm
done