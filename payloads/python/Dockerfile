#
# todo: add this to the buld as 'make payload'
#
# Note
# - requires running 'make pack' in $TOP/km
# - Lib/test is removed (takes 20+M). Need to add it to dockeringore
# - dockeignore is not complete, still copies tons of unneeded stuff
#
#
# issues:
# - some syscals can still kill the damn thing (as they are dynamically called from modules
# - platform dependedn libs and modules need to be tunes/removed
#
# To build
#     docker build -t km:python .
# To run
#     docker run --rm -it --device=/dev/kvm km:python -c '<code>'
# or add a volume with py files
#

FROM kontain:km

LABEL Description="Starts Python in Kontain VM" Vendor="Kontain.app" Version="0.1"

COPY cpython/python.km python/
COPY cpython/Lib/ python/Lib/
COPY cpython/Modules/Setup python/Modules/

ENTRYPOINT [ "/km" , "/python/python.km" ]
