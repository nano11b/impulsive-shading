#!/bin/sh
set -eu
printf '%s  %s\n' "$(cat /tmp/impulsive-marks-download.sha256)" /tmp/impulsive-marks-download.zip | sha256sum -c -
if ! id marks >/dev/null 2>&1; then
  useradd --system --home /var/lib/impulsive-marks --shell /usr/sbin/nologin marks
fi
install -d -m 0755 /opt/impulsive-marks
install -d -o marks -g marks -m 0750 /var/lib/impulsive-marks
install -d -o marks -g marks -m 0750 /var/lib/impulsive-marks/reports
install -d -o root -g marks -m 0750 /etc/impulsive-marks
if [ ! -s /etc/impulsive-marks/upload-token ]; then
  umask 077
  openssl rand -hex 32 > /etc/impulsive-marks/upload-token
fi
chown root:marks /etc/impulsive-marks/upload-token
chmod 0640 /etc/impulsive-marks/upload-token
install -o root -g root -m 0644 /tmp/impulsive-marks-server.py /opt/impulsive-marks/server.py
install -o root -g root -m 0644 /tmp/impulsive-marks-index.html /opt/impulsive-marks/index.html
install -o root -g root -m 0644 /tmp/impulsive-marks-download.html /opt/impulsive-marks/download.html
install -o root -g root -m 0644 /tmp/impulsive-marks-download.sha256 /opt/impulsive-marks/download.sha256
install -o root -g root -m 0644 /tmp/impulsive-marks-download.zip /opt/impulsive-marks/Impulsive-Shading-Benchmark-Win64.zip
printf '%s  %s\n' "$(cat /tmp/impulsive-marks-linux-download.sha256)" /tmp/impulsive-marks-linux-download.tar.gz | sha256sum -c -
install -o root -g root -m 0644 /tmp/impulsive-marks-linux-download.sha256 /opt/impulsive-marks/linux-download.sha256
install -o root -g root -m 0644 /tmp/impulsive-marks-linux-download.tar.gz /opt/impulsive-marks/Impulsive-Shading-Benchmark-Linux-x86_64.tar.gz
install -o root -g root -m 0644 /tmp/impulsive-marks.service /etc/systemd/system/impulsive-marks.service
systemctl daemon-reload
systemctl enable --now impulsive-marks.service
systemctl restart impulsive-marks.service
