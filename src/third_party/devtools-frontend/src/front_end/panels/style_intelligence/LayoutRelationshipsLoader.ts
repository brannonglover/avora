// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Node-scoped layout relationships. Not used by StyleIntelligenceModel.describe.
 * The command calls Blink. This loader does not reconstruct containing blocks
 * or scroll containers from CSS, and it does not capture a document snapshot.
 */

import * as SDK from '../../core/sdk/sdk.js';

import {readLayoutRelationships, type LayoutRelationships} from './LayoutRelationships.js';

export async function loadLayoutRelationships(node: SDK.DOMModel.DOMNode): Promise<LayoutRelationships|null> {
  if (node.nodeType() !== Node.ELEMENT_NODE || !node.id) {
    return null;
  }
  const cssModel = node.domModel().cssModel();
  if (!cssModel) {
    return null;
  }
  // Typed against the generated bindings on purpose. If the Chromium protocol
  // patch is ever lost in an upgrade, this stops compiling instead of silently
  // returning nothing at runtime.
  const response = await cssModel.getAgent().invoke_getLayoutRelationshipsForNode({nodeId: node.id});
  if (response.getError()) {
    return null;
  }
  return readLayoutRelationships(response.relationships ?? null);
}
