package cr.ac.tec.ic7602.dnsapi.dns;

/**
  verificar si un dominio tiene un registro almacenado
  cambiar de proveedor (Supabase/Firebase) sin tocar los handlers 
 */
public interface DnsExistsChecker {
    boolean exists(String domain);
}
