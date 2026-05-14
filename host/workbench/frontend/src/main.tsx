import React from 'react';
import ReactDOM from 'react-dom/client';
import { ConfigProvider, theme } from 'antd';
import App from './App';
import './styles/app.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <ConfigProvider
      theme={{
        algorithm: theme.darkAlgorithm,
        token: {
          colorPrimary: '#4ea37b',
          colorInfo: '#4a9eff',
          colorWarning: '#faad14',
          colorError: '#ff6b6b',
          colorSuccess: '#4ea37b',
          colorBgBase: '#0d1318',
          colorTextBase: '#edf3f8',
          borderRadius: 3,
          borderRadiusLG: 3,
          fontFamily:
            '"IBM Plex Sans", "Aptos", "Segoe UI", ui-sans-serif, system-ui, sans-serif',
          fontFamilyCode:
            '"JetBrains Mono", "SFMono-Regular", ui-monospace, monospace'
        },
        components: {
          Layout: {
            bodyBg: '#0d1318',
            headerBg: '#0f171d',
            siderBg: '#101920'
          },
          Card: {
            headerHeightSM: 38
          },
          Menu: {
            darkItemBg: '#101920',
            darkSubMenuItemBg: '#101920'
          }
        }
      }}
    >
      <App />
    </ConfigProvider>
  </React.StrictMode>
);
