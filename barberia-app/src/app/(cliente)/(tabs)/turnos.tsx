// Mis turnos: próximos (con reprogramar/cancelar) e historial
import { router } from 'expo-router';
import { useState } from 'react';
import { Alert, View } from 'react-native';

import { AppointmentCard } from '@/components/appointment-card';
import { Button, Card, Loading, Screen, T } from '@/components/ui';
import { spacing } from '@/constants/theme';
import { cancelAppointment, fetchMyAppointments } from '@/lib/booking';
import { fetchSettings, useLoad } from '@/lib/data';
import { canClientChange, formatDateTime } from '@/lib/dates';
import { friendlyError } from '@/lib/supabase';
import { useAuth } from '@/providers/auth';

export default function MyAppointments() {
  const { profile } = useAuth();
  const [cancelling, setCancelling] = useState<string | null>(null);
  const { data, error, loading, reload } = useLoad(async () => {
    const [appts, settings] = await Promise.all([fetchMyAppointments(profile!.id), fetchSettings()]);
    return { ...appts, limit: settings.cancel_limit_hours };
  });

  function askCancel(id: string, when: string) {
    Alert.alert('Cancelar turno', `¿Seguro que querés cancelar tu turno del ${when}?`, [
      { text: 'No', style: 'cancel' },
      {
        text: 'Sí, cancelar',
        style: 'destructive',
        onPress: async () => {
          setCancelling(id);
          try {
            await cancelAppointment(id);
            await reload();
          } catch (e) {
            Alert.alert('No se pudo cancelar', friendlyError(e instanceof Error ? e.message : String(e)));
          } finally {
            setCancelling(null);
          }
        },
      },
    ]);
  }

  if (loading && !data) return <Loading />;
  const limit = data?.limit ?? 2;

  return (
    <Screen>
      <T variant="title">Mis turnos</T>
      {error ? <T variant="error">{friendlyError(error)}</T> : null}

      <T variant="subtitle">Próximos</T>
      {data?.upcoming.length === 0 ? (
        <Card>
          <T variant="muted">No tenés turnos reservados.</T>
          <Button label="Reservar turno" onPress={() => router.push('/reservar')} />
        </Card>
      ) : (
        data?.upcoming.map((a) => {
          const canChange = canClientChange(a.starts_at, limit);
          return (
            <AppointmentCard key={a.id} a={a} highlight>
              {canChange ? (
                <>
                  <Button label="Reprogramar" variant="outline" onPress={() => router.push({ pathname: '/reservar', params: { reprogramar: a.id } })} />
                  <Button label="Cancelar turno" variant="danger" loading={cancelling === a.id} onPress={() => askCancel(a.id, formatDateTime(a.starts_at))} />
                </>
              ) : (
                <T variant="muted">Faltan menos de {limit} horas: para cambios escribinos por WhatsApp.</T>
              )}
            </AppointmentCard>
          );
        })
      )}

      <T variant="subtitle" style={{ marginTop: spacing.md }}>Historial</T>
      {data?.history.length === 0 ? <T variant="muted">Todavía no tenés turnos anteriores.</T> : null}
      <View style={{ gap: spacing.sm }}>
        {data?.history.map((a) => <AppointmentCard key={a.id} a={a} />)}
      </View>
    </Screen>
  );
}
