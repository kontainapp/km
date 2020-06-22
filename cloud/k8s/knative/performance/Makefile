run:
	ko create -f load-test-setup.yaml
	ko create -f load-test.yaml

clean:
	ko delete -f load-test.yaml
	ko delete -f load-test-setup.yaml
