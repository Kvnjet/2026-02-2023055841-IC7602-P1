package cr.ac.tec.ic7602.dnsapi.dns;

/**
 * Verifica si un dominio tiene un registro almacenado.
 * Permite cambiar de proveedor (Supabase/Firebase) sin tocar los handlers.
 */
public interface DnsExistsChecker {
    boolean exists(String domain);
}
