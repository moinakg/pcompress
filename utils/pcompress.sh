#!/bin/sh

PC_PATH="<PC_PATH>"
LD_LIBRARY_PATH="${PC_PATH}"
export LD_LIBRARY_PATH

exec ${PC_PATH}/buildtmp/pcompress "$@"

