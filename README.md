# env-dedup
Deduplicate the environment variables values
---

# Information
C dynamic library to add to `LD_PRELOAD` to clean environment variables that are duplicated.
Both versions contains a list of environment variables that it is allowed to change. Add or remove according to your needs!

## Static version
The static version cleans all environment variables every time

## Dynamic version
The dynamic version cleans only the environment variable that is being changed

# Usage

## Build
- To build the static version use:
`gcc -shared -fPIC -o libenv_dedup.so env_dedup.c -ldl`
- To build the dynamic version use:
`gcc -shared -fPIC -o libenv_dedup_dynamic.so env_dedup_dynamic.c -ldl`

## Apply
Add the dynamic library to `LD_PRELOAD` like this and test it:
`LD_PRELOAD=libenv_dedup_dynamic.so $app`

### If everything works
Consider adding exporting it on `.bashrc` or for KDE to `~/.config/plasma-workspace/env/00-env-dedup.sh`

### On NixOS
You can add:
`environment.variables.LD_PRELOAD = "/etc/nixos/libenv_dedup_dynamic.so";`
to make it system wide.
(adjust the location as needed)

# Considerations
Please test carefully first before applying system wide

## Development
This code was developed with the help of AI/LLM.
