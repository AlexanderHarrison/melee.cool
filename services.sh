#!/bin/bash

cp server.service /etc/systemd/system/
cp backup.service /etc/systemd/system/
systemctl reenable server.service
systemctl reenable backup.service
systemctl restart server.service
systemctl restart backup.service
