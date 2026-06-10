#!/bin/bash

cp server.service /etc/systemd/system/
cp backup.service /etc/systemd/system/
cp certbot.service /etc/systemd/system/
cp certbot.timer /etc/systemd/system/
systemctl reenable server.service
systemctl reenable backup.service
systemctl reenable certbot.timer
systemctl restart server.service
systemctl restart backup.service
systemctl restart certbot.timer
