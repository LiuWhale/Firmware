#
# Makefile for the px4fmu_default configuration
#

CONFIG		 = px4fmu_default
PLATFORM	 = px4fmu
SRCS		 = $(PX4_BASE)/platforms/empty.c

include $(PX4_BASE)/makefiles/firmware.mk
