#!/bin/bash

mkdir -p backups/
zip -r9 backups/clips-$(date +"%m_%d_%Y").zip clips/
