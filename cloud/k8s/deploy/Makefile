knative-cluster:
	- sudo systemctl stop k3s
	- sudo systemctl disable k3s
	curl -sfL https://get.k3s.io | INSTALL_K3S_EXEC="--no-deploy traefik" INSTALL_K3S_VERSION="v1.24.3+k3s1" sh -s - --write-kubeconfig-mode 644
	sudo systemctl enable --now k3s
	sudo systemctl start k3s
	# sudo chmod 777 /etc/rancher/k3s/k3s.yaml
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml

	until systemctl is-active k3s; do echo waiting; done

	# after a lot of testing, found that we have to wait > 15 seconds for k3s to start instantiating pods in kube-system for core k8s
	sleep 30
	k3s kubectl wait --for=condition=Ready pods --all -n kube-system

	# ref: https://knative.dev/docs/install/yaml-install/serving/install-serving-with-yaml/#prerequisites

	# 1. Install the required custom resources by running the command:
	echo install knative serving component crds
	k3s kubectl apply -f https://github.com/knative/serving/releases/download/knative-v1.4.0/serving-crds.yaml

	sleep 5

	# 2. Install the core components of Knative Serving by running the command:
	echo install knative serving core components
	k3s kubectl apply -f https://github.com/knative/serving/releases/download/knative-v1.4.0/serving-core.yaml

	k3s kubectl wait --for=condition=Ready pods --all -n knative-serving
	sleep 5

	# networking layer
	# 1. Install the Knative Kourier controller by running the command:
	echo installing kourier controller
	k3s kubectl apply -f https://github.com/knative/net-kourier/releases/download/knative-v1.4.0/kourier.yaml

	k3s kubectl wait --for=condition=Ready pods --all -n knative-serving
	sleep 5

	# 2. Configure Knative Serving to use Kourier by default by running the command:
	echo configuring knative serving to use Kourier by default
	k3s kubectl patch configmap/config-network \
					--namespace knative-serving \
					--type merge \
					--patch '{"data":{"ingress-class":"kourier.ingress.networking.knative.dev"}}'

	sleep 5

	k3s kubectl patch configmap/config-features \
		-n knative-serving \
		--type merge \
		-p '{"data":{"kubernetes.podspec-runtimeclassname": "enabled"}}'
	sleep 5

	# 3. Fetch the External IP address or CNAME by running the command:
	echo getting external IP or CNAME
	k3s kubectl --namespace kourier-system get service kourier

	# 1. Configure DNS to use Magic DNS (sslip.io) so as not use to curl with Host header
	# Knative provides a Kubernetes Job called default-domain that configures Knative Serving to use sslip.io as the default DNS suffix.
	# echo configuring k8s default-domain job to use magic DNS - sslip.io
	k3s kubectl apply -f https://github.com/knative/serving/releases/download/knative-v1.4.0/serving-default-domain.yaml

	echo waiting for pods to become ready
	k3s kubectl rollout status deployment domain-mapping -n knative-serving

	echo list the pods
	k3s kubectl get pods -n knative-serving

knative-cluster-clean:
	/usr/local/bin/k3s-uninstall.sh
	# remove the krun containerd shim from host
	sudo rm -f /usr/local/bin/containerd-shim-krun-v2

knative-service-list:
	k3s kubectl proxy &
	curl -XGET http://localhost:8001/apis/serving.knative.dev/v1/namespaces/default/services

status-kontain:
	k3s kubectl logs daemonset/kontain-node-initializer -n kube-system
	k3s kubectl get po -A

apply-kontain:
	k3s kubectl apply -f https://raw.githubusercontent.com/kontainapp/guide-examples/master/infra/kustomize_outputs/k3s.yaml
	sleep 20
	sudo systemctl restart k3s
	sleep 20

remove-kontain:
	k3s kubectl delete daemonset/kontain-node-initializer -n kube-system

deploy-k8s:
	echo
	echo "deploying py-flask-hello to cluster kind..."
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml
	k3s kubectl apply -f testservices/py-flask-hello-k8s.yml
	sleep 10
	k3s kubectl -n default wait pod --for=condition=Ready -l app=py-flask-hello --timeout=240s
	sleep 5

test-k8s:
	echo
	echo "testing py-flask-hello..."
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml
	- pkill -f "port-forward"
	k3s kubectl port-forward svc/py-flask-hello 5000:5000 2>/dev/null &
	sleep 20
	curl -vvv http://localhost:5000
	echo
	sleep 5
	- pkill -f "port-forward"
	sleep 5

delete-k8s:
	k3s kubectl delete -f testservices/py-flask-hello-k8s.yml

deploy-kn:
	echo
	- export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service delete hello
	echo "deploy hello-kontain service for test..."

	# kn service create hello \
	# 	--image docker.io/kontainguide/golang-http-hello:1.0 \
	# 	--port 8080 \
	# 	--env TARGET=World

	k3s kubectl apply -f testservices/hello-kontain-svc.yml

	echo waiting for service to become ready...
	sleep 10
	echo "listing services..."
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service list

test-kn:
	echo
	echo "invoking service..."
	echo Accessing URL at: $$(export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service describe hello-kontain -o url)
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && curl $$(kn service describe hello-kontain -o url)

delete-kn:
	k3s kubectl delete -f testservices/hello-kontain-svc.yml


deploy-kn2:
	echo
	- export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service delete py-flask-hello-svc
	echo "deploy py-flask-hello-svc service for test..."

	# kn service create hello \
	# 	--image docker.io/kontainguide/golang-http-hello:1.0 \
	# 	--port 8080 \
	# 	--env TARGET=World

	k3s kubectl apply -f testservices/py-flask-hello-svc.yml

	echo waiting for service to become ready...
	sleep 10
	echo "listing services..."
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service list

test-kn2:
	echo
	echo "invoking service..."
	echo Accessing URL at: $$(export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service describe py-flask-hello-svc -o url)
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && curl $$(kn service describe py-flask-hello-svc -o url)

delete-kn2:
	k3s kubectl delete -f testservices/hello-kontain-svc.yml

deploy-kn3:
	echo
	- export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service delete py-flask-hello-svc
	echo "deploy spring-boot-hello-svc service for test..."

	# kn service create hello \
	# 	--image docker.io/kontainguide/golang-http-hello:1.0 \
	# 	--port 8080 \
	# 	--env TARGET=World

	k3s kubectl apply -f testservices/spring-boot-hello-svc.yml

	echo waiting for service to become ready...
	sleep 20
	echo "listing services..."
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service list

test-kn3:
	echo
	echo "invoking service..."
	echo Accessing URL at: $$(export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && kn service describe spring-boot-hello-svc -o url)
	export KUBECONFIG=/etc/rancher/k3s/k3s.yaml && curl $$(kn service describe spring-boot-hello-svc -o url)

delete-kn3:
	k3s kubectl delete -f testservices/spring-boot-hello-svc.yml
