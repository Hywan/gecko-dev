/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Cc, Ci, Cu, Cr } = require("chrome");
const promise = require("sdk/core/promise");
const EventEmitter = require("devtools/shared/event-emitter");
const { PageSummaryFront } = require("devtools/server/actors/pagesummary");

function PageSummaryPanel(iframeWindow, toolbox) {
  this.panelWin = iframeWindow;
  this._toolbox = toolbox;
  this._destroyer = null;

  EventEmitter.decorate(this);
};

exports.PageSummaryPanel = PageSummaryPanel;

PageSummaryPanel.prototype = {
  open: function() {
    let targetPromise;

    // Local debugging needs to make the target remote.
    if (!this.target.isRemote) {
      targetPromise = this.target.makeRemote();
    } else {
      targetPromise = promise.resolve(this.target);
    }

    return targetPromise
      .then(() => {
        this.panelWin.start(new PageSummaryFront(this.target.client, this.target.form));
      })
      .then(() => {
        this.isReady = true;
        this.emit("ready");
        return this;
      })
      .then(null, function onError(aReason) {
        Cu.reportError("PageSummaryPanel open failed. " +
                       aReason.error + ": " + aReason.message);
      });
  },

  // DevToolPanel API

  get target() this._toolbox.target,

  destroy: function() {
    if (!this._destroyed) {
      return;
    }
    this._destroyed = true;
    this.emit("destroyed");
  }
};
