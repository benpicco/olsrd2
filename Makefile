BUILD_DIR=build

.ONESHELL:
all: default_target FORCE
	@make -s -C $(BUILD_DIR) all

doc: default_target FORCE
	@make -s -C $(BUILD_DIR) doc

test: default_target FORCE
	@make -s -C $(BUILD_DIR) test

clean: default_target FORCE
	@rm -rf build

default_target:
	@test -d ${BUILD_DIR} || sh -c "mkdir $(BUILD_DIR) ; cd $(BUILD_DIR) ;  cmake .."

FORCE:
