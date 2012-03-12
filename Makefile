BUILD_DIR=build

.ONESHELL:
.PHONY: cmake

all: cmake FORCE
	@${MAKE} -s -C ${BUILD_DIR} all
 
clean:
	@rm -rf build

cmake:
	@test -e ${BUILD_DIR}/Makefile || sh -c "rm -rf ${BUILD_DIR}; mkdir $(BUILD_DIR) ; cd $(BUILD_DIR) ;  cmake .."

FORCE:

.DEFAULT: cmake FORCE
	@test -e ${BUILD_DIR}/Makefile || sh -c "rm -rf ${BUILD_DIR}; mkdir $(BUILD_DIR) ; cd $(BUILD_DIR) ;  cmake .."
	@${MAKE} -s -C ${BUILD_DIR} $@
