package cr.ac.tec.ic7602.dnsapi.dns;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;

/**
 * Cliente UDP/DNS: reenvía el paquete DNS crudo (en bytes) al servidor remoto
 * configurado y retorna la respuesta cruda, sin interpretar su contenido.
 * El paquete viaja en BASE64 en los bordes HTTP y como UDP crudo en el medio.
 *
 * Cada llamada abre su propio socket, así que es seguro usarlo desde varios hilos.
 */
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

    /**
     * Envía {@code queryBytes} al servidor DNS remoto y devuelve la respuesta cruda.
     * @throws DnsResolutionException si hay timeout o error de red.
     */
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
        private static final long serialVersionUID = 1L;
        public DnsResolutionException(String message, Throwable cause) {
            super(message, cause);
        }
    }
}
