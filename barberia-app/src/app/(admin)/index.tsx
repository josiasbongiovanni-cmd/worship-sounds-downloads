import { Brand, Card, Screen, T } from '@/components/ui';
import { useAuth } from '@/providers/auth';

export default function AdminAgenda() {
  const { profile } = useAuth();
  return (
    <Screen>
      <Brand size="small" />
      <T variant="title">Panel de administración</T>
      <Card highlight>
        <T variant="gold">Hola {profile?.full_name.split(' ')[0]}, entraste como administrador ✅</T>
        <T variant="muted">La agenda del día y de la semana se arma en la Fase 3.</T>
      </Card>
    </Screen>
  );
}
