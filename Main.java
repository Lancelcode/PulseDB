public class Main {
  public static void main(String[] args) {
    int port = 6379;

    if (args.length > 0) {
      port = Integer.parseInt(args[0]);
    }

    new RedisServer(port).start();
  }
}