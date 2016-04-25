CXX=g++
LD=g++

default: release

include make-utils/flags-pi.mk
include make-utils/cpp-utils.mk

pi.conf:
	echo "user=pi" > pi.conf
	echo "pi=192.168.20.161" >> pi.conf
	echo "password=raspberry" >> pi.conf
	echo "dir=/home/pi/asgard/asgard-system-driver/" >> pi.conf

conf: pi.conf

include pi.conf

CXX_FLAGS += -pedantic -pthread -Iasgard-lib/include/

$(eval $(call auto_folder_compile,src))
$(eval $(call auto_add_executable,system_driver))

release: release_system_driver
release_debug: release_debug_system_driver
debug: debug_system_driver

all: release release_debug debug

run: release
	./release/bin/system_driver

remote_clean:
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make clean"

remote_make:
	sshpass -p ${password} scp -p Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp -p src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} scp -p asgard-lib/include/asgard/*.hpp ${user}@${pi}:${dir}/asgard-lib/include/asgard/
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make"

remote_run:
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make run"

remote_make_run:
	sshpass -p ${password} scp -p Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp -p src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} scp -p asgard-lib/include/asgard/*.hpp ${user}@${pi}:${dir}/asgard-lib/include/asgard/
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make && make run"

clean: base_clean

include make-utils/cpp-utils-finalize.mk

.PHONY: default release_debug release debug all clean conf
