const Cu = Components.utils;
const Ci = Components.interfaces;
const Cc = Components.classes;

Cu.import('resource://gre/modules/Services.jsm');
Cu.import("resource://gre/modules/Webapps.jsm");

const FORCE_APP_RELOAD_AT_STARTUP = true;

const AppsService = Cc["@mozilla.org/AppsService;1"].getService(Ci.nsIAppsService);

function onLoad() {
  window.removeEventListener("DOMContentLoaded", onLoad);

  if (Services.prefs.prefHasUserValue("htmlrunner.rootAppSource")) {
    document.querySelector("#core").removeAttribute("hidden");
    document.querySelector("#init").setAttribute("hidden", "true");
    Services.obs.addObserver(LoadBrowserApp, "htmlrunner-rootapp-installed", false);

    if (FORCE_APP_RELOAD_AT_STARTUP) {
      appReload();
    } else {
      LoadBrowserApp();
    }

  } else {
    document.querySelector("#init").removeAttribute("hidden");
    document.querySelector("#core").setAttribute("hidden", "true");
  }
}

window.addEventListener("DOMContentLoaded", onLoad);

function LoadBrowserApp() {
  let manifestURL = Services.prefs.getCharPref("htmlrunner.rootAppManifest");
  let app = AppsService.getAppByManifestURL(manifestURL);
  let iframe = document.querySelector("iframe");
  iframe.focus();
  if (app) {
    AppsService.getManifestFor(manifestURL).then(manifest => {
      let url = app.origin + manifest.launch_path;
      let docShell = iframe.contentWindow
                           .QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIWebNavigation)
                           .QueryInterface(Ci.nsIDocShell);
      docShell.setIsApp(app.localId);

      if (iframe.getAttribute("src") == url) {
        iframe.contentWindow.location.reload();
      } else {
        iframe.setAttribute("src", url);
      }
    });
  }
}

function appReload() {
  // Fake first run, which will trigger re-installation of Firefox.html.
  Services.prefs.clearUserPref("gecko.mstone");
  DOMApplicationRegistry.loadAndUpdateApps();
}

function setAppCodePath() {
  let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
  fp.init(window, "Select App Path", Ci.nsIFilePicker.modeGetFolder);
  let rv = fp.show();
  if (rv == Ci.nsIFilePicker.returnOK) {
    Services.prefs.setCharPref("htmlrunner.rootAppSource", fp.file.path);
    if (!FORCE_APP_RELOAD_AT_STARTUP) {
      appReload();
    }
    onLoad();
  }
}

function startDevtools() {
  // connecting to the app directory doesn't work yet.
  // The issue is the devtools assume a html iframe, with an associated
  // message manager that it can't reach.
  const MAIN_PROCESS = true;

  Cu.import("resource:///modules/devtools/gDevTools.jsm");
  const {devtools} = Cu.import("resource://gre/modules/devtools/Loader.jsm", {});
  const {require} = devtools;
  const {Connection} = require("devtools/client/connection-manager");
  const {AppManager} = require("devtools/webide/app-manager");

  let manifestURL = Services.prefs.getCharPref("htmlrunner.rootAppManifest");

  if (!MAIN_PROCESS) {
    let FramesMock = {
      list: function () {
        let iframe = document.querySelector("iframe");
        iframe.setAttribute("mozapp", manifestURL);
        return [iframe];
      },
      addObserver: function () {},
      removeObserver: function () {}
    };
    require("devtools/server/actors/webapps").setFramesMock(FramesMock);
  }

  let connectionInitialized = false;
  AppManager.on("app-manager-update", function onAppMgrUpdate(event,what,details) {

    if (what == "runtimelist") {
      if (connectionInitialized)
        return;
      for (let runtime of AppManager.runtimeList.other) {
        if (runtime.type == "LOCAL") {
          connectionInitialized = true;
          AppManager.connectToRuntime(runtime);
          return;
        }
      }
    }

    if (!MAIN_PROCESS && what == "runtime-apps-found") {
      AppsService.getManifestFor(manifestURL).then(manifest => {
        manifest.manifestURL = manifestURL;
        AppManager.selectedProject = {
          type: "runtimeApp",
          app: manifest,
          icon: "",
          name: manifest.name
        }
        AppManager.getTarget().then(t => {
          console.log("target", t);
        }, e => {
          console.error(e);
        });
      });
    }

    if (MAIN_PROCESS && what == "list-tabs-response") {
      AppManager.selectedProject = {
        type: "mainProcess",
        name: "mainProcess"
      }
      AppManager.getTarget().then(target => {
        console.log(target);
        gDevTools.showToolbox(target, "webconsole", "window");
      });
    }
  });

  AppManager.init();
}
