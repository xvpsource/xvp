#
# Making the VNC applet.
#
# Extensions to support XVP server Copyright (C) 2009 Colin Dean. 
#

CP = cp
JC = javac -J-Xmx16m
JCFLAGS = -target 1.5 -source 1.5 -nowarn
JAR = jar -J-Xmx16m
ARCHIVE = VncViewer.jar
MANIFEST = MANIFEST.MF
PAGES = index.vnc

MKDIR = mkdir -p
GZIP = gzip
CHMOD = chmod
INSTALL = install -p
SCRIPT = xvpviewer
BATCH = xvpviewer.bat
BINDIR = /usr/bin
DATADIR = /usr/share/xvp

CLASSES = VncViewer.class RfbProto.class AuthPanel.class VncCanvas.class \
	  VncCanvas2.class \
	  OptionsFrame.class ClipboardFrame.class ButtonPanel.class \
	  DesCipher.class CapabilityInfo.class CapsContainer.class \
	  RecordingFrame.class SessionRecorder.class \
	  SocketFactory.class ReloginPanel.class \
	  HTTPConnectSocketFactory.class HTTPConnectSocket.class \
	  HTTPSConnectSocketFactory.class HTTPSConnectSocket.class \
	  InStream.class MemInStream.class ZlibInStream.class \
	  XvpConfirmDialog.class X11Keysyms.class

SOURCES = VncViewer.java RfbProto.java AuthPanel.java VncCanvas.java \
	  VncCanvas2.java \
	  OptionsFrame.java ClipboardFrame.java ButtonPanel.java \
	  DesCipher.java CapabilityInfo.java CapsContainer.java \
	  RecordingFrame.java SessionRecorder.java \
	  SocketFactory.java ReloginPanel.java \
	  HTTPConnectSocketFactory.java HTTPConnectSocket.java \
	  HTTPSConnectSocketFactory.java HTTPSConnectSocket.java \
	  InStream.java MemInStream.java ZlibInStream.java \
	  XvpConfirmDialog.java X11Keysyms.java

all: $(CLASSES) $(ARCHIVE)

$(CLASSES): $(SOURCES)
	$(JC) $(JCFLAGS) -O $(SOURCES)

$(ARCHIVE): $(CLASSES) $(MANIFEST)
	$(JAR) cfm $(ARCHIVE) $(MANIFEST) $(CLASSES)

install: $(ARCHIVE) $(SCRIPT) $(BATCH)
	$(MKDIR) $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)
	$(INSTALL) -m 0755 $(SCRIPT) $(DESTDIR)$(BINDIR)/$(SCRIPT)
	$(INSTALL) -m 0644 $(BATCH) $(DESTDIR)$(DATADIR)/$(BATCH)
	$(INSTALL) -m 0644 $(ARCHIVE) $(DESTDIR)$(DATADIR)/$(ARCHIVE)

export:: $(CLASSES) $(ARCHIVE) $(PAGES)
	@$(ExportJavaClasses)

clean::
	$(RM) *.class *.jar
