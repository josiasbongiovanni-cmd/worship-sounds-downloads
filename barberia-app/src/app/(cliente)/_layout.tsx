import { Tabs } from 'expo-router/js-tabs';

import { tabIcon, tabScreenOptions } from '@/components/tab-bar';

export default function ClientTabs() {
  return (
    <Tabs screenOptions={tabScreenOptions}>
      <Tabs.Screen name="index" options={{ title: 'Inicio', tabBarIcon: tabIcon('home') }} />
      <Tabs.Screen name="turnos" options={{ title: 'Mis turnos', tabBarIcon: tabIcon('calendar') }} />
      <Tabs.Screen name="tarjeta" options={{ title: 'Mi tarjeta', tabBarIcon: tabIcon('ribbon') }} />
      <Tabs.Screen name="perfil" options={{ title: 'Perfil', tabBarIcon: tabIcon('person') }} />
    </Tabs>
  );
}
