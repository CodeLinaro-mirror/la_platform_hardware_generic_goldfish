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

import { describe, it, expect, vi, beforeEach } from 'vitest';
import React from 'react';
import { renderToString } from 'react-dom/server';
import { AquariumClient } from '../src/core/client';
import { AquariumProvider, useAquariumClient } from '../src/react/context';
import { useAquarium, useSensors, useVm } from '../src/react/hooks';
import { EmulatorView } from '../src/react/EmulatorView';

describe('React Integration (TDD)', () => {
  let client: AquariumClient;

  beforeEach(() => {
    client = AquariumClient.create({
      endpoint: 'http://localhost:8085',
      fetch: vi.fn(),
    });
  });

  it('AquariumProvider exposes client to context consumers', () => {
    let capturedClient: AquariumClient | null = null;

    function TestChild() {
      capturedClient = useAquariumClient();
      return <div>Test</div>;
    }

    const html = renderToString(
      <AquariumProvider client={client}>
        <TestChild />
      </AquariumProvider>
    );

    expect(html).toContain('Test');
    expect(capturedClient).toBe(client);
  });

  it('useAquariumClient throws when rendered outside of AquariumProvider', () => {
    function OrphanChild() {
      useAquariumClient();
      return null;
    }

    expect(() => renderToString(<OrphanChild />)).toThrow(
      'useAquariumClient must be used within an <AquariumProvider>'
    );
  });

  it('EmulatorView renders video canvas and container element', () => {
    const html = renderToString(
      <AquariumProvider client={client}>
        <EmulatorView className="custom-emulator-class" />
      </AquariumProvider>
    );

    expect(html).toContain('<video');
    expect(html).toContain('custom-emulator-class');
  });
});
