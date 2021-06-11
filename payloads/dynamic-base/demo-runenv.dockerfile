ARG runenv_image_version=latest
FROM kontainapp/runenv-dynamic-base:${runenv_image_version}
COPY hello_test.kmd hello_test
CMD ["hello_test", "Hello, World!", "I'm dynamic"]