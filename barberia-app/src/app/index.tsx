// Pantalla intermedia: hay sesión pero todavía no cargó (o no existe) el perfil
import { Button, Card, Loading, Screen, T } from '@/components/ui';
import { useAuth } from '@/providers/auth';

export default function ProfileGate() {
  const { profileError, refreshProfile, signOut } = useAuth();
  if (!profileError) return <Loading message="Cargando tu cuenta..." />;
  return (
    <Screen>
      <Card>
        <T variant="subtitle">No pudimos cargar tu cuenta</T>
        <T variant="muted">{profileError}</T>
        <T variant="muted">Si sos el dueño: revisá que hayas corrido los archivos SQL en Supabase.</T>
      </Card>
      <Button label="Reintentar" onPress={refreshProfile} />
      <Button label="Cerrar sesión" variant="outline" onPress={signOut} />
    </Screen>
  );
}
