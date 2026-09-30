// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import React, { useState, useCallback, useEffect, useRef } from 'react';
import { createRoot } from 'react-dom/client';
import {
  DeviceProvider,
  DeviceView,
  useDevice,
  useSensors,
  usePosture,
  useCapabilities,
  useLocation,
  useVm,
  RtcConnectionState,
  KeyAction,
  InputFeature,
  TransitionMode,
  PostureType,
} from '@android/web';

interface LogEntry {
  id: number;
  time: string;
  type: 'info' | 'warn' | 'error' | 'success';
  message: string;
}

/**
 * Main Aquarium demo application showcasing the architectural patterns in DESIGN.md:
 * - Exactly one visual UI component: <EmulatorView displayId={0} />
 * - Surrounding headless control panels powered by React hooks:
 *   - useAquarium: connection lifecycle and WebRTC state
 *   - useSensors: orientation, accelerometer, ambient light, temperature
 *   - useCapabilities: device display, hardware feature discovery
 *   - useLocation: geographic coordinates and GPS emulation
 *   - useVm: virtual machine lifecycle control
 */
function DeviceDemoApp({ onLog }: { onLog: (msg: string, type?: LogEntry['type']) => void }) {
  const { state, connect, disconnect, client } = useDevice();
  const {
    sensors,
    fetchSensors,
    setPoseRotation,
    setAccelerometer,
    clearRawOverrides,
    setAmbientLight,
    setTemperature,
    setProximity,
    isBrowserStreaming,
    startBrowserSensorStream,
    stopBrowserSensorStream,
    isUpdating: isSensorsUpdating,
  } = useSensors();
  const { setHingeAngle, setDevicePosture } = usePosture();
  const { capabilities, fetchCapabilities, isLoading: isCapsLoading } = useCapabilities();
  const { setLocation, isUpdating: isLocUpdating } = useLocation();
  const { pause, resume, isBusy: isVmBusy } = useVm();

  const [activeTab, setActiveTab] = useState<'controls' | 'sensors' | 'location' | 'capabilities' | 'vm'>('controls');
  const [pitchVal, setPitchVal] = useState<number>(0);
  const [yawVal, setYawVal] = useState<number>(0);
  const [rollVal, setRollVal] = useState<number>(0);
  const [transitionMode, setTransitionMode] = useState<TransitionMode>(
    TransitionMode.TRANSITION_MODE_SMOOTH
  );
  const [hingeVal, setHingeVal] = useState<number>(180);
  const [lightVal, setLightVal] = useState<number>(100);
  const [tempVal, setTempVal] = useState<number>(22);
  const [latInput, setLatInput] = useState<string>('37.4220');
  const [lngInput, setLngInput] = useState<string>('-122.0841');

  // Track WebRTC connection transitions
  const prevStateRef = useRef<RtcConnectionState | null>(null);
  useEffect(() => {
    if (prevStateRef.current !== state) {
      prevStateRef.current = state;
      onLog(`WebRTC Connection State: ${state}`, state === RtcConnectionState.CONNECTED ? 'success' : 'info');
    }
  }, [state, onLog]);

  // Automatically connect and reconnect whenever not connected or when the remote peer closes the DataChannel
  const connectedSinceRef = useRef<number>(0);
  useEffect(() => {
    if (state === RtcConnectionState.CONNECTED) {
      connectedSinceRef.current = Date.now();
    } else {
      connectedSinceRef.current = 0;
    }
    const timer = setInterval(() => {
      if (state === RtcConnectionState.CONNECTED) {
        if (!client.input.isReady && connectedSinceRef.current > 0 && Date.now() - connectedSinceRef.current > 4000) {
          onLog('Remote DataChannel closed; reconnecting...', 'warn');
          disconnect();
        }
        return;
      }
      if (state !== RtcConnectionState.CONNECTING) {
        connect().catch((err) => onLog(`Auto-connect retry: ${err.message}`, 'warn'));
      }
    }, 2000);
    return () => clearInterval(timer);
  }, [state, client, connect, disconnect, onLog]);

  // Android Navigation key sender
  const sendKey = useCallback(
    async (domCode: string, name: string) => {
      if (!client.input.isReady) {
        onLog(`Cannot send key '${name}': Input channel not ready`, 'warn');
        return;
      }
      onLog(`Send Key: ${name} (${domCode})`);
      client.input.sendKey({ action: KeyAction.KEY_ACTION_DOWN, domCode });
      setTimeout(() => {
        client.input.sendKey({ action: KeyAction.KEY_ACTION_UP, domCode });
      }, 50);
    },
    [client, onLog]
  );

  return (
    <div style={styles.container}>
      {/* Top Header Bar */}
      <header style={styles.header}>
        <div style={styles.headerLeft}>
          <h1 style={styles.title}>
            📱 <span style={{ color: '#60a5fa' }}>Device Stream</span>
            <span style={styles.badge}>Web SDK v0.1.0</span>
          </h1>
        </div>

        <div style={styles.headerRight}>
          <span
            style={{
              ...styles.statusBadge,
              backgroundColor:
                state === RtcConnectionState.CONNECTED
                  ? 'rgba(16, 185, 129, 0.15)'
                  : state === RtcConnectionState.CONNECTING
                  ? 'rgba(245, 158, 11, 0.15)'
                  : 'rgba(239, 68, 68, 0.15)',
              color:
                state === RtcConnectionState.CONNECTED
                  ? '#34d399'
                  : state === RtcConnectionState.CONNECTING
                  ? '#fbbf24'
                  : '#f87171',
              border: `1px solid ${
                state === RtcConnectionState.CONNECTED
                  ? '#10b981'
                  : state === RtcConnectionState.CONNECTING
                  ? '#f59e0b'
                  : '#ef4444'
              }`,
            }}
          >
            ● {state.toUpperCase()}
          </span>

          {state === RtcConnectionState.CONNECTED ? (
            <button style={styles.btnDanger} onClick={disconnect}>
              Disconnect
            </button>
          ) : (
            <button
              style={styles.btnPrimary}
              onClick={() => {
                onLog('Connecting to WebRTC media stream...');
                connect().catch((err) => onLog(`Connection error: ${err.message}`, 'error'));
              }}
              disabled={state === RtcConnectionState.CONNECTING}
            >
              {state === RtcConnectionState.CONNECTING ? 'Connecting...' : 'Connect Stream'}
            </button>
          )}
        </div>
      </header>

      {/* Main Workspace: Side Control Panels + Center Visual Component */}
      <div style={styles.content}>
        {/* Left Side Headless Panels */}
        <aside style={styles.sidebar}>
          {/* Navigation Tabs */}
          <div style={styles.tabs}>
            <button
              style={activeTab === 'controls' ? styles.tabActive : styles.tab}
              onClick={() => setActiveTab('controls')}
            >
              Controls
            </button>
            <button
              style={activeTab === 'sensors' ? styles.tabActive : styles.tab}
              onClick={() => setActiveTab('sensors')}
            >
              Sensors
            </button>
            <button
              style={activeTab === 'location' ? styles.tabActive : styles.tab}
              onClick={() => setActiveTab('location')}
            >
              GPS
            </button>
            <button
              style={activeTab === 'capabilities' ? styles.tabActive : styles.tab}
              onClick={() => setActiveTab('capabilities')}
            >
              Device
            </button>
            <button
              style={activeTab === 'vm' ? styles.tabActive : styles.tab}
              onClick={() => setActiveTab('vm')}
            >
              VM
            </button>
          </div>

          <div style={styles.panelBody}>
            {/* 1. Android Controls Tab */}
            {activeTab === 'controls' && (
              <div style={styles.section}>
                <h3 style={styles.sectionTitle}>Android Hardware Keys</h3>
                <p style={styles.sectionDesc}>Sends evdev key events over SCTP DataChannel.</p>
                <div style={styles.btnGrid}>
                  <button style={styles.btnSecondary} onClick={() => sendKey('GoBack', 'Back')}>
                    ◀ Back
                  </button>
                  <button style={styles.btnSecondary} onClick={() => sendKey('GoHome', 'Home')}>
                    ● Home
                  </button>
                  <button style={styles.btnSecondary} onClick={() => sendKey('AppSwitch', 'Recents')}>
                    ■ Recents
                  </button>
                  <button style={styles.btnSecondary} onClick={() => sendKey('Power', 'Power')}>
                    ⏻ Power
                  </button>
                  <button style={styles.btnSecondary} onClick={() => sendKey('AudioVolumeUp', 'Vol+')}>
                    🔊 Vol +
                  </button>
                  <button style={styles.btnSecondary} onClick={() => sendKey('AudioVolumeDown', 'Vol-')}>
                    🔉 Vol -
                  </button>
                </div>
              </div>
            )}

            {/* 2. V2 Sensors & Posture Tab */}
            {activeTab === 'sensors' && (
              <div style={styles.section}>
                <h3 style={styles.sectionTitle}>V2 Sensors & Physical Simulation</h3>
                <p style={styles.sectionDesc}>
                  Drives 3D pose & ambient environment over WebRTC DataChannel (&quot;sensors_v2&quot;) or gRPC-Web.
                </p>

                {/* WebRTC Browser Physical Sensor Streaming */}
                <div style={styles.controlRow}>
                  <button
                    style={{
                      ...(isBrowserStreaming ? styles.btnDanger : styles.btnPrimary),
                      width: '100%',
                    }}
                    onClick={() => {
                      if (isBrowserStreaming) {
                        stopBrowserSensorStream();
                        onLog('Stopped client browser orientation streaming');
                      } else {
                        startBrowserSensorStream();
                        onLog(
                          'Started client browser orientation streaming over WebRTC DataChannel ("sensors_v2")',
                          'success'
                        );
                      }
                    }}
                  >
                    {isBrowserStreaming
                      ? '⏹ Stop Browser Sensor Stream (WebRTC)'
                      : '📡 Stream Browser Orientation (WebRTC)'}
                  </button>
                </div>

                {/* Transition Mode Toggle */}
                <div style={styles.controlRow}>
                  <label style={styles.label}>Kinematics Transition Mode:</label>
                  <div style={styles.btnRow}>
                    <button
                      style={
                        transitionMode === TransitionMode.TRANSITION_MODE_SMOOTH
                          ? styles.btnPrimary
                          : styles.btnSmall
                      }
                      onClick={() => setTransitionMode(TransitionMode.TRANSITION_MODE_SMOOTH)}
                    >
                      Smooth (Natural Physics)
                    </button>
                    <button
                      style={
                        transitionMode === TransitionMode.TRANSITION_MODE_STEP
                          ? styles.btnPrimary
                          : styles.btnSmall
                      }
                      onClick={() => setTransitionMode(TransitionMode.TRANSITION_MODE_STEP)}
                    >
                      Step (Instant Jump)
                    </button>
                  </div>
                </div>

                {/* 3D Pose Orientation Presets */}
                <div style={styles.controlRow}>
                  <label style={styles.label}>3D Pose Presets:</label>
                  <div style={styles.btnGrid}>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(0);
                        setYawVal(0);
                        setRollVal(0);
                        onLog('Setting 3D Pose: Portrait (0°, 0°, 0°)');
                        setPoseRotation({ pitch: 0, yaw: 0, roll: 0 }, transitionMode);
                      }}
                    >
                      Portrait
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(0);
                        setYawVal(0);
                        setRollVal(90);
                        onLog('Setting 3D Pose: Landscape (0°, 0°, 90°)');
                        setPoseRotation({ pitch: 0, yaw: 0, roll: 90 }, transitionMode);
                      }}
                    >
                      Landscape
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(0);
                        setYawVal(0);
                        setRollVal(180);
                        onLog('Setting 3D Pose: Upside Down (0°, 0°, 180°)');
                        setPoseRotation({ pitch: 0, yaw: 0, roll: 180 }, transitionMode);
                      }}
                    >
                      Upside Down
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(0);
                        setYawVal(0);
                        setRollVal(-90);
                        onLog('Setting 3D Pose: Seascape (0°, 0°, -90°)');
                        setPoseRotation({ pitch: 0, yaw: 0, roll: -90 }, transitionMode);
                      }}
                    >
                      Seascape
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(-90);
                        setYawVal(0);
                        setRollVal(0);
                        onLog('Setting 3D Pose: Flat Face-Up (-90°, 0°, 0°)');
                        setPoseRotation({ pitch: -90, yaw: 0, roll: 0 }, transitionMode);
                      }}
                    >
                      Flat Up
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setPitchVal(90);
                        setYawVal(0);
                        setRollVal(0);
                        onLog('Setting 3D Pose: Flat Face-Down (90°, 0°, 0°)');
                        setPoseRotation({ pitch: 90, yaw: 0, roll: 0 }, transitionMode);
                      }}
                    >
                      Flat Down
                    </button>
                  </div>
                </div>

                {/* 3D Euler Angles Scrubbing over WebRTC DataChannel */}
                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Pitch (X):</label>
                    <span style={styles.sliderVal}>{pitchVal}°</span>
                  </div>
                  <input
                    type="range"
                    min="-180"
                    max="180"
                    step="1"
                    value={pitchVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setPitchVal(v);
                      setPoseRotation({ pitch: v, yaw: yawVal, roll: rollVal }, transitionMode);
                    }}
                  />
                </div>

                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Yaw / Azimuth (Y):</label>
                    <span style={styles.sliderVal}>{yawVal}°</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="360"
                    step="1"
                    value={yawVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setYawVal(v);
                      setPoseRotation({ pitch: pitchVal, yaw: v, roll: rollVal }, transitionMode);
                    }}
                  />
                </div>

                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Roll (Z):</label>
                    <span style={styles.sliderVal}>{rollVal}°</span>
                  </div>
                  <input
                    type="range"
                    min="-90"
                    max="90"
                    step="1"
                    value={rollVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setRollVal(v);
                      setPoseRotation({ pitch: pitchVal, yaw: yawVal, roll: v }, transitionMode);
                    }}
                  />
                </div>

                {/* Live Synthesized IMU Telemetry Readout */}
                <div style={styles.codeBlock}>
                  <div style={{ color: '#93c5fd', fontWeight: 600, marginBottom: '4px' }}>
                    Live IMU Telemetry (Guest HAL)
                  </div>
                  <div>
                    Accel (m/s²): ({(sensors?.imu?.accelerometer?.x ?? 0).toFixed(2)},{' '}
                    {(sensors?.imu?.accelerometer?.y ?? 0).toFixed(2)},{' '}
                    {(sensors?.imu?.accelerometer?.z ?? 0).toFixed(2)})
                  </div>
                  <div>
                    Gyro (rad/s): ({(sensors?.imu?.gyroscope?.x ?? 0).toFixed(2)},{' '}
                    {(sensors?.imu?.gyroscope?.y ?? 0).toFixed(2)},{' '}
                    {(sensors?.imu?.gyroscope?.z ?? 0).toFixed(2)})
                  </div>
                  <div>
                    Mag (µT): ({(sensors?.imu?.magnetometer?.x ?? 0).toFixed(1)},{' '}
                    {(sensors?.imu?.magnetometer?.y ?? 0).toFixed(1)},{' '}
                    {(sensors?.imu?.magnetometer?.z ?? 0).toFixed(1)})
                  </div>
                  {sensors?.imu?.activeOverrides?.length ? (
                    <div style={{ color: '#fbbf24', marginTop: '4px' }}>
                      Active Overrides: {sensors.imu.activeOverrides.length} pinned sensor(s)
                    </div>
                  ) : null}
                </div>

                {/* Raw Sensor Overrides (Fault Injection) */}
                <div style={styles.controlRow}>
                  <label style={styles.label}>Raw Sensor Overrides (Bypass Physics):</label>
                  <div style={styles.btnRow}>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        onLog('Injecting 0g Freefall raw override (Accel = 0,0,0)');
                        setAccelerometer({ x: 0, y: 0, z: 0 });
                      }}
                      disabled={isSensorsUpdating}
                    >
                      0g Freefall Override
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        onLog('Clearing raw sensor overrides (returning to physics model)');
                        clearRawOverrides();
                      }}
                      disabled={isSensorsUpdating}
                    >
                      Clear Overrides
                    </button>
                  </div>
                </div>

                {/* Ambient Environment Controls */}
                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Ambient Light:</label>
                    <span style={styles.sliderVal}>{lightVal} Lux</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="2000"
                    step="50"
                    value={lightVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setLightVal(v);
                      setAmbientLight(v);
                    }}
                  />
                </div>

                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Ambient Temperature:</label>
                    <span style={styles.sliderVal}>{tempVal} °C</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="50"
                    step="1"
                    value={tempVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setTempVal(v);
                      setTemperature(v);
                    }}
                  />
                </div>

                <div style={styles.controlRow}>
                  <label style={styles.label}>Proximity Sensor:</label>
                  <div style={styles.btnRow}>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        onLog('Setting proximity: Near (0 cm)');
                        setProximity(0);
                      }}
                    >
                      Near (0 cm)
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        onLog('Setting proximity: Far (5 cm)');
                        setProximity(5);
                      }}
                    >
                      Far (5 cm)
                    </button>
                  </div>
                </div>

                {/* Foldable Posture & Hinge Controls */}
                <div style={styles.controlRow}>
                  <div style={styles.sliderHeader}>
                    <label style={styles.label}>Foldable Hinge Angle:</label>
                    <span style={styles.sliderVal}>{hingeVal}°</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="180"
                    step="5"
                    value={hingeVal}
                    style={styles.range}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      setHingeVal(v);
                      setHingeAngle(v, 0);
                    }}
                  />
                  <div style={{ ...styles.btnGrid, marginTop: '4px' }}>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setHingeVal(0);
                        onLog('Setting Posture: Closed (0°)');
                        setDevicePosture(PostureType.POSTURE_TYPE_CLOSED);
                      }}
                    >
                      Closed
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setHingeVal(90);
                        onLog('Setting Posture: Half-Opened (90°)');
                        setDevicePosture(PostureType.POSTURE_TYPE_HALF_OPENED);
                      }}
                    >
                      Half-Open
                    </button>
                    <button
                      style={styles.btnSmall}
                      onClick={() => {
                        setHingeVal(180);
                        onLog('Setting Posture: Opened (180°)');
                        setDevicePosture(PostureType.POSTURE_TYPE_OPENED);
                      }}
                    >
                      Opened
                    </button>
                  </div>
                </div>

                <button
                  style={{ ...styles.btnSecondary, marginTop: '8px', width: '100%' }}
                  onClick={async () => {
                    onLog('Fetching current V2 SensorsState from guest OS...');
                    try {
                      const s = await fetchSensors();
                      onLog(
                        `Sensors snapshot: Temp=${s.environment?.ambientTemperatureCelsius}°C, Light=${s.environment?.lightLux}lx`,
                        'success'
                      );
                    } catch (err: any) {
                      onLog(`Failed to fetch sensors: ${err.message}`, 'error');
                    }
                  }}
                >
                  Query Live Sensors
                </button>
              </div>
            )}

            {/* 3. Location Tab */}
            {activeTab === 'location' && (
              <div style={styles.section}>
                <h3 style={styles.sectionTitle}>Geolocation / GPS</h3>
                <p style={styles.sectionDesc}>Simulate coordinates via useLocation() hook.</p>

                <div style={styles.controlRow}>
                  <label style={styles.label}>Latitude:</label>
                  <input
                    type="text"
                    style={styles.input}
                    value={latInput}
                    onChange={(e) => setLatInput(e.target.value)}
                  />
                </div>

                <div style={styles.controlRow}>
                  <label style={styles.label}>Longitude:</label>
                  <input
                    type="text"
                    style={styles.input}
                    value={lngInput}
                    onChange={(e) => setLngInput(e.target.value)}
                  />
                </div>

                <div style={styles.btnRow}>
                  <button
                    style={styles.btnPrimary}
                    onClick={() => {
                      const lat = parseFloat(latInput);
                      const lng = parseFloat(lngInput);
                      if (isNaN(lat) || isNaN(lng)) {
                        onLog('Invalid coordinates', 'error');
                        return;
                      }
                      onLog(`Setting GPS Location: ${lat}, ${lng}`);
                      setLocation({ latitude: lat, longitude: lng }).catch((err) =>
                        onLog(`Location error: ${err.message}`, 'error')
                      );
                    }}
                    disabled={isLocUpdating}
                  >
                    Apply GPS Location
                  </button>
                  <button
                    style={styles.btnSecondary}
                    onClick={() => {
                      setLatInput('37.4220');
                      setLngInput('-122.0841');
                      onLog('Setting GPS Location: Googleplex (37.4220, -122.0841)');
                      setLocation({ latitude: 37.422, longitude: -122.0841 });
                    }}
                  >
                    Googleplex
                  </button>
                </div>
              </div>
            )}

            {/* 4. Device Capabilities Tab */}
            {activeTab === 'capabilities' && (
              <div style={styles.section}>
                <h3 style={styles.sectionTitle}>Device Capabilities</h3>
                <p style={styles.sectionDesc}>Discovers displays and features via useCapabilities().</p>
                <button
                  style={{ ...styles.btnPrimary, width: '100%', marginBottom: '12px' }}
                  onClick={async () => {
                    onLog('Calling CapabilitiesService.GetDeviceCapabilities()...');
                    try {
                      const caps = await fetchCapabilities();
                      onLog(`Capabilities fetched for: ${caps?.metadata?.displayName || 'Android Device'}`, 'success');
                    } catch (err: any) {
                      onLog(`Capabilities error: ${err.message}`, 'error');
                    }
                  }}
                  disabled={isCapsLoading}
                >
                  {isCapsLoading ? 'Querying...' : 'Query Device Capabilities'}
                </button>

                {capabilities ? (
                  <div style={styles.codeBlock}>
                    <div style={{ color: '#93c5fd', fontWeight: 600, marginBottom: '4px' }}>
                      {capabilities.metadata?.displayName || 'Android Emulator'}
                    </div>
                    <div>Model: {capabilities.metadata?.modelId || 'Virtual Device'}</div>
                    {capabilities.display?.builtInDisplays?.map((d, i) => (
                      <div key={i} style={{ marginLeft: '8px', color: '#94a3b8' }}>
                        Display #{d.displayId}: {d.width}x{d.height} @ {d.densityDpi} DPI ({d.refreshRateHz}Hz)
                      </div>
                    ))}
                    <div style={{ marginTop: '6px' }}>
                      Sensors: {capabilities.sensors?.availableSensors?.length ? `${capabilities.sensors.availableSensors.length} sensor(s)` : 'Standard'}
                    </div>
                    <div>
                      Touch: {capabilities.input?.supportedFeatures?.includes(InputFeature.INPUT_FEATURE_MULTI_TOUCH) ? 'Multi-Touch' : 'Basic'}
                    </div>
                  </div>
                ) : (
                  <div style={styles.emptyState}>No capabilities loaded yet. Click Query above.</div>
                )}
              </div>
            )}

            {/* 5. VM Lifecycle Tab */}
            {activeTab === 'vm' && (
              <div style={styles.section}>
                <h3 style={styles.sectionTitle}>Virtual Machine Lifecycle</h3>
                <p style={styles.sectionDesc}>Pause or resume the guest OS using useVm().</p>
                <div style={styles.btnRow}>
                  <button
                    style={styles.btnSecondary}
                    onClick={() => {
                      onLog('Pausing VM execution...');
                      pause()
                        .then(() => onLog('VM paused successfully', 'success'))
                        .catch((err) => onLog(`Pause failed: ${err.message}`, 'error'));
                    }}
                    disabled={isVmBusy}
                  >
                    ⏸ Pause VM
                  </button>
                  <button
                    style={styles.btnSecondary}
                    onClick={() => {
                      onLog('Resuming VM execution...');
                      resume()
                        .then(() => onLog('VM resumed successfully', 'success'))
                        .catch((err) => onLog(`Resume failed: ${err.message}`, 'error'));
                    }}
                    disabled={isVmBusy}
                  >
                    ▶ Resume VM
                  </button>
                </div>
              </div>
            )}
          </div>
        </aside>

        {/* Center: The Single Visual UI Component Footprint */}
        <main style={styles.main}>
          <div style={styles.emulatorWrapper}>
            <DeviceView
              displayId={0}
              autoConnect={true}
              onError={(err) => onLog(`DeviceView error: ${err.message}`, 'error')}
            />
          </div>
        </main>
      </div>
    </div>
  );
}

/**
 * Root Application Container providing endpoint configuration and the AquariumProvider.
 */
function RootApp() {
  const defaultEndpoint =
    typeof window !== 'undefined' && window.location.origin.startsWith('http')
      ? window.location.origin
      : 'http://localhost:8085';

  const [endpoint, setEndpoint] = useState<string>(defaultEndpoint);
  const [token, setToken] = useState<string>('');
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const logIdRef = useRef<number>(0);
  const logsEndRef = useRef<HTMLDivElement | null>(null);

  const addLog = useCallback((message: string, type: LogEntry['type'] = 'info') => {
    const time = new Date().toISOString().split('T')[1].slice(0, 8);
    const entry: LogEntry = {
      id: ++logIdRef.current,
      time,
      type,
      message,
    };
    setLogs((prev) => [...prev.slice(-150), entry]);
  }, []);

  useEffect(() => {
    logsEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  return (
    <div style={styles.rootContainer}>
      {/* Top Configuration Ribbon */}
      <div style={styles.configBar}>
        <div style={styles.configItem}>
          <label style={styles.configLabel}>Endpoint:</label>
          <input
            type="text"
            style={styles.configInput}
            value={endpoint}
            onChange={(e) => setEndpoint(e.target.value)}
          />
        </div>

        <div style={styles.configItem}>
          <label style={styles.configLabel}>JWT Token:</label>
          <input
            type="password"
            placeholder="Bearer eyJhbGci..."
            style={{ ...styles.configInput, width: '180px' }}
            value={token}
            onChange={(e) => setToken(e.target.value)}
          />
        </div>

        <div style={{ marginLeft: 'auto', display: 'flex', gap: '8px' }}>
          <button
            style={styles.btnSmall}
            onClick={() => setLogs([])}
          >
            Clear Logs
          </button>
        </div>
      </div>

      {/* DeviceProvider wrapping the app */}
      <div style={styles.bodyWrapper}>
        <DeviceProvider uri={endpoint} token={token || undefined}>
          <DeviceDemoApp onLog={addLog} />
        </DeviceProvider>
      </div>

      {/* Bottom Collapsible Diagnostic Log Console */}
      <footer style={styles.footerConsole}>
        <div style={styles.consoleHeader}>
          <span>Diagnostic Log Console</span>
          <span style={{ fontSize: '0.75rem', color: '#64748b' }}>{logs.length} entries</span>
        </div>
        <div style={styles.consoleBody}>
          {logs.map((l) => (
            <div
              key={l.id}
              style={{
                ...styles.logLine,
                color:
                  l.type === 'error'
                    ? '#f87171'
                    : l.type === 'warn'
                    ? '#fbbf24'
                    : l.type === 'success'
                    ? '#34d399'
                    : '#93c5fd',
              }}
            >
              <span style={styles.logTime}>[{l.time}]</span> {l.message}
            </div>
          ))}
          <div ref={logsEndRef} />
        </div>
      </footer>
    </div>
  );
}

// Inline Styles for a polished dark theme
const styles: Record<string, React.CSSProperties> = {
  rootContainer: {
    display: 'flex',
    flexDirection: 'column',
    width: '100vw',
    height: '100vh',
    overflow: 'hidden',
    backgroundColor: '#0a0d14',
  },
  configBar: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
    padding: '6px 16px',
    backgroundColor: '#111827',
    borderBottom: '1px solid #1f2937',
    fontSize: '0.85rem',
  },
  configItem: {
    display: 'flex',
    alignItems: 'center',
    gap: '6px',
  },
  configLabel: {
    color: '#9ca3af',
    fontWeight: 500,
    fontSize: '0.8rem',
  },
  configInput: {
    backgroundColor: '#1f2937',
    border: '1px solid #374151',
    color: '#e5e7eb',
    padding: '4px 8px',
    borderRadius: '4px',
    fontSize: '0.8rem',
    width: '220px',
  },
  bodyWrapper: {
    flex: 1,
    display: 'flex',
    minHeight: 0,
    overflow: 'hidden',
  },
  container: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    height: '100%',
    overflow: 'hidden',
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '10px 16px',
    backgroundColor: '#131c2e',
    borderBottom: '1px solid #1e293b',
  },
  headerLeft: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
  },
  title: {
    margin: 0,
    fontSize: '1.15rem',
    fontWeight: 600,
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
  },
  badge: {
    fontSize: '0.7rem',
    backgroundColor: '#1e293b',
    color: '#60a5fa',
    padding: '2px 8px',
    borderRadius: '12px',
    border: '1px solid #3b82f6',
    fontWeight: 500,
  },
  headerRight: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
  },
  statusBadge: {
    fontSize: '0.75rem',
    fontWeight: 600,
    padding: '3px 10px',
    borderRadius: '12px',
    letterSpacing: '0.04em',
  },
  content: {
    display: 'flex',
    flex: 1,
    minHeight: 0,
    overflow: 'hidden',
  },
  sidebar: {
    width: '320px',
    backgroundColor: '#0f172a',
    borderRight: '1px solid #1e293b',
    display: 'flex',
    flexDirection: 'column',
    minHeight: 0,
  },
  tabs: {
    display: 'flex',
    borderBottom: '1px solid #1e293b',
    backgroundColor: '#0b1120',
  },
  tab: {
    flex: 1,
    padding: '8px 4px',
    backgroundColor: 'transparent',
    border: 'none',
    borderBottom: '2px solid transparent',
    color: '#94a3b8',
    fontSize: '0.8rem',
    cursor: 'pointer',
    fontWeight: 500,
  },
  tabActive: {
    flex: 1,
    padding: '8px 4px',
    backgroundColor: 'transparent',
    border: 'none',
    borderBottom: '2px solid #3b82f6',
    color: '#60a5fa',
    fontSize: '0.8rem',
    cursor: 'pointer',
    fontWeight: 600,
  },
  panelBody: {
    flex: 1,
    overflowY: 'auto',
    padding: '12px',
  },
  section: {
    display: 'flex',
    flexDirection: 'column',
    gap: '8px',
  },
  sectionTitle: {
    margin: '0 0 4px 0',
    fontSize: '0.9rem',
    color: '#f1f5f9',
  },
  sectionDesc: {
    margin: '0 0 8px 0',
    fontSize: '0.75rem',
    color: '#64748b',
    lineHeight: '1.3',
  },
  btnGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(3, 1fr)',
    gap: '6px',
  },
  btnRow: {
    display: 'flex',
    gap: '6px',
  },
  controlRow: {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    marginBottom: '8px',
  },
  label: {
    fontSize: '0.75rem',
    color: '#94a3b8',
    fontWeight: 500,
  },
  sliderHeader: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  sliderVal: {
    fontSize: '0.75rem',
    color: '#60a5fa',
    fontWeight: 600,
  },
  range: {
    width: '100%',
    accentColor: '#3b82f6',
  },
  input: {
    backgroundColor: '#1e293b',
    border: '1px solid #334155',
    color: '#f8fafc',
    padding: '6px 8px',
    borderRadius: '4px',
    fontSize: '0.8rem',
  },
  main: {
    flex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#020617',
    position: 'relative',
    overflow: 'hidden',
  },
  emulatorWrapper: {
    width: '100%',
    height: '100%',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
  },
  footerConsole: {
    height: '140px',
    backgroundColor: '#0b1120',
    borderTop: '1px solid #1e293b',
    display: 'flex',
    flexDirection: 'column',
    overflow: 'hidden',
  },
  consoleHeader: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: '4px 12px',
    backgroundColor: '#0f172a',
    borderBottom: '1px solid #1e293b',
    fontSize: '0.75rem',
    fontWeight: 600,
    color: '#94a3b8',
  },
  consoleBody: {
    flex: 1,
    overflowY: 'auto',
    padding: '6px 12px',
    fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace',
    fontSize: '0.75rem',
    lineHeight: '1.4',
  },
  logLine: {
    whiteSpace: 'pre-wrap',
    wordBreak: 'break-all',
  },
  logTime: {
    color: '#475569',
    marginRight: '6px',
  },
  btnPrimary: {
    backgroundColor: '#2563eb',
    color: '#ffffff',
    border: 'none',
    padding: '6px 12px',
    borderRadius: '4px',
    fontSize: '0.8rem',
    fontWeight: 600,
    cursor: 'pointer',
  },
  btnSecondary: {
    backgroundColor: '#1e293b',
    color: '#e2e8f0',
    border: '1px solid #334155',
    padding: '6px 10px',
    borderRadius: '4px',
    fontSize: '0.75rem',
    fontWeight: 500,
    cursor: 'pointer',
  },
  btnDanger: {
    backgroundColor: '#dc2626',
    color: '#ffffff',
    border: 'none',
    padding: '6px 12px',
    borderRadius: '4px',
    fontSize: '0.8rem',
    fontWeight: 600,
    cursor: 'pointer',
  },
  btnSmall: {
    backgroundColor: '#1e293b',
    color: '#94a3b8',
    border: '1px solid #334155',
    padding: '4px 8px',
    borderRadius: '4px',
    fontSize: '0.75rem',
    cursor: 'pointer',
  },
  codeBlock: {
    backgroundColor: '#020617',
    border: '1px solid #1e293b',
    padding: '8px',
    borderRadius: '4px',
    fontSize: '0.75rem',
    fontFamily: 'monospace',
    lineHeight: '1.4',
  },
  emptyState: {
    color: '#64748b',
    fontSize: '0.75rem',
    fontStyle: 'italic',
    textAlign: 'center',
    padding: '16px 0',
  },
};

// Mount root React application
const container = document.getElementById('root');
if (container) {
  const root = createRoot(container);
  root.render(<RootApp />);
}
