import { Tabs } from 'expo-router/js-tabs';

import { tabIcon, tabScreenOptions } from '@/components/tab-bar';

export default function AdminTabs() {
  return (
    <Tabs screenOptions={tabScreenOptions}>
      <Tabs.Screen name="index" options={{ title: 'Agenda', tabBarIcon: tabIcon('today') }} />
      <Tabs.Screen name="servicios" options={{ title: 'Servicios', tabBarIcon: tabIcon('cut') }} />
      <Tabs.Screen name="clientes" options={{ title: 'Clientes', tabBarIcon: tabIcon('people') }} />
      <Tabs.Screen name="perfil" options={{ title: 'Perfil', tabBarIcon: tabIcon('person') }} />
    </Tabs>
  );
}
