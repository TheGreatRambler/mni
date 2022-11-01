for d in */ ; do
	echo Building ${d}main.owasm
	# Build using emcc without filesystem or entry
	emcc -Oz -I../mni/include -sFILESYSTEM=0 -sERROR_ON_UNDEFINED_SYMBOLS=0 --no-entry ${d}main.cpp -o ${d}main.wasm
	# Covert to optimized wasm
	echo mni compile -o ${d}main.owasm ${d}main.wasm
	../build/mni.exe compile -o ${d}main.owasm ${d}main.wasm
	# Start runtime
	echo mni run --wasm ${d}main.owasm
	../build/mni.exe run --wasm ${d}main.owasm
	# Remove both binaries
	if [[ -z $1 ]] || [[ "$1" != "nodelete" ]]; then
		rm ${d}main.owasm
		rm ${d}main.wasm
	fi
done