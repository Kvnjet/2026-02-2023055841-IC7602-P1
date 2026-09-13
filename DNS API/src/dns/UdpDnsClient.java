package cr.ac.tec.ic7602.dnsapi.dns;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;

public class UdpDnsClient {

    private static final int MAX_DNS_PACKET_SIZE = 4096;

    private final String remoteHost;
    private final int remotePort;
    private final int timeoutMs;

    public UdpDnsClient(String remoteHost, int remotePort, int timeoutMs) {
        this.remoteHost = remoteHost;
        this.remotePort = remotePort;
        this.timeoutMs = timeoutMs;
    }


     @throws DnsResolutionException 
     
    public byte[] resolve(byte[] queryBytes) throws DnsResolutionException {
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setSoTimeout(timeoutMs);

            InetAddress address = InetAddress.getByName(remoteHost);
            DatagramPacket request = new DatagramPacket(queryBytes, queryBytes.length, address, remotePort);
            socket.send(request);

            byte[] buffer = new byte[MAX_DNS_PACKET_SIZE];
            DatagramPacket response = new DatagramPacket(buffer, buffer.length);
            socket.receive(response);

            byte[] result = new byte[response.getLength()];
            System.arraycopy(response.getData(), 0, result, 0, response.getLength());
            return result;

        } catch (SocketTimeoutException e) {
            throw new DnsResolutionException(
                    "Timeout esperando respuesta de " + remoteHost + ":" + remotePort, e);
        } catch (IOException e) {
            throw new DnsResolutionException(
                    "Error de red al consultar " + remoteHost + ":" + remotePort, e);
        }
    }

    public static class DnsResolutionException extends Exception {
        public DnsResolutionException(String message, Throwable cause) {
            super(message, cause);
        }
    }
}
