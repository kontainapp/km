FROM kontain/runenv-jdk-11.0.8:latest
COPY springboot-poc-0.0.1-SNAPSHOT.jar springboot-poc-0.0.1-SNAPSHOT.jar
COPY cmd.sh cmd.sh
EXPOSE 9090/tcp
ENV POSTGRE_URL jdbc:postgresql://localhost:5432
ENV POSTGRE_USER postgres
ENV POSTGRE_PWD nopass
ENV REDIS_URL redis://localhost:6379
