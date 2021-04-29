FROM alpine
# note: 'alpine' (instead of 'scratch') adds 6MB to Node's 43MB.
# It helps with troubleshooting and using shell in further dockerfiles

COPY . /usr/local/bin/
