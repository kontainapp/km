ARG RUNENV_IMAGE_VERSION=latest
FROM kontainapp/runenv-dynamic-base-large:${RUNENV_IMAGE_VERSION}
COPY hello_test.kmd hello_test
CMD ["hello_test", "Hello, World!", "I'm dynamic"]
