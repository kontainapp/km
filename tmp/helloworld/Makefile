TOP := /home/msterin/workspace/km

RUN := docker run --device=/dev/kvm --rm -it -p 8000:8000 -v /opt/kontain:/opt/kontain -v ${TOP}:${TOP} --name alp alp

run:
	$(RUN)

ash:
	$(RUN) ash

build:
	docker build -t alp . 

kill: 
	docker rm -f alp
