#!/bin/bash

# Check to see if docker is functional
docker ps > /dev/null
if [[ $? -ne 0 ]]; then
	exit 1
fi

# Attempt the builds for Ubuntu
for RELEASE in 20.04 22.04 23.04 23.10; do
	echo -n "Building for Ubuntu $RELEASE..."
	docker build --network=host --build-arg RELEASE=$RELEASE -f Dockerfile.ubuntu -t stack:ubuntu-$RELEASE . >&/dev/null
	if [[ $? -eq 0 ]]; then
		echo " Success"
	else
		echo " Failed"
	fi
done

# Attempt the build for Rocky Linux
for RELEASE in 8 9; do
	echo -n "Building for Rocky Linux $RELEASE..."
	docker build --network=host --build-arg RELEASE=$RELEASE -f Dockerfile.rocky -t stack:rocky-$RELEASE . >&/dev/null
	if [[ $? -eq 0 ]]; then
		echo " Success"
	else
		echo " Failed"
	fi
done
