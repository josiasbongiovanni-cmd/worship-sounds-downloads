import { ServicesList } from '@/components/services-list';
import { Card, Loading, Screen, T } from '@/components/ui';
import { fetchServices, useLoad } from '@/lib/data';

export default function AdminServices() {
  const { data, error, loading } = useLoad(fetchServices);
  if (loading && !data) return <Loading />;
  return (
    <Screen>
      <T variant="title">Servicios</T>
      <Card><T variant="muted">Crear y editar servicios llega en la Fase 3. Por ahora se ven los cargados.</T></Card>
      {error ? <T variant="error">{error}</T> : null}
      {data ? <ServicesList services={data} /> : null}
    </Screen>
  );
}
