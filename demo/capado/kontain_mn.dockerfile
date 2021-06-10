FROM kontainapp/runenv-jdk-11.0.8:latest
COPY micronaut-poc-0.1-all.jar micronaut-poc-0.1-all.jar
EXPOSE 9091/tcp

ARG POSTGRES_IP=172.17.0.2

# Replace 172.17.0.2 with your IP address
ENV POSTGRE_URL=jdbc:postgresql://$POSTGRES_IP:5432/micronaut
ENV POSTGRE_USER=postgres
ENV POSTGRE_PWD=nopass
ENV REDIS_URL=redis://localhost:6379
