import { Card, Screen, T } from '@/components/ui';

export default function MyAppointments() {
  return (
    <Screen>
      <T variant="title">Mis turnos</T>
      <Card>
        <T variant="muted">Acá vas a ver tus próximos turnos y tu historial de cortes. (Fase 2)</T>
      </Card>
    </Screen>
  );
}
