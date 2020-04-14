#   Build alpine container we can extract all needed alpine libs from

ARG FROM_IMAGE=alpine:latest

FROM $FROM_IMAGE

RUN apk add bash make git g++ gcc musl-dev libffi-dev
