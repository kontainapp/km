/*
 * A simple static http server using only Java SE dependencies.
 *
 * To test: curl localhost:8080
*/

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;

public class SimpleHttpServer {

  public static void main(String[] args) throws Exception {
		System.out.println("Kontain Java server listening on 8080");

    HttpServer server = HttpServer.create(new InetSocketAddress(8080), 0);
    server.createContext("/", new MyHandler());
    server.setExecutor(null); // creates a default executor
    server.start();
  }

  static class MyHandler implements HttpHandler {
    public void handle(HttpExchange t) throws IOException {
      byte [] response = "Welcome to Kotain.app Java demo page".getBytes();
      t.sendResponseHeaders(200, response.length);
      OutputStream os = t.getResponseBody();
      os.write(response);
      os.close();
    }
  }
}