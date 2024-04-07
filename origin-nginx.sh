#!/bin/bash
/usr/sbin/nginx -c $PWD/origin-nginx/nginx.conf -g 'daemon off;'
