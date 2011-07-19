BUILD_DIR=build


.ONESHELL:
all: default_target FORCE
	@make -s -C $(BUILD_DIR) all

clean: default_target FORCE
	@make -s -C $(BUILD_DIR) clean

autotest:
	@./test_all.sh

default_target:
	@test -d ${BUILD_DIR} || sh -c "mkdir $(BUILD_DIR) ; cd $(BUILD_DIR) ;  cmake .."

FORCE:
