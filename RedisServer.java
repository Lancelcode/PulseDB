import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;

public class RedisServer {
  private final int port;
  private final RedisStore store = new RedisStore();

  public RedisServer(int port) {
    this.port = port;
  }

  public void start() {
    try (ServerSocket serverSocket = new ServerSocket(port)) {
      serverSocket.setReuseAddress(true);
      System.out.println("Redis server listening on port " + port);

      while (true) {
        Socket clientSocket = serverSocket.accept();
        new Thread(new ClientHandler(clientSocket, store)).start();
      }

    } catch (IOException e) {
      System.out.println("IOException: " + e.getMessage());
    }
  }
}