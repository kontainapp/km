# Dummy Dockerfile for tests. We do not really build runenv-image here, 
# but upper level Makefiles do scan this dir and for consistency, let's create dummy 
# runenv-images 

FROM scratch

LABEL COMMENT "Dummy Image, just for making build target consistent"
