host     := "ftracker-aws"
domain   := "sync.utils.pemsoft.org"
port     := "38471"
image    := "obsidian-sync-arm64"
bin      := "obsidian-sync-server"
email    := "dmitrylor@gmail.com"

default:
    @just --list

build:
    docker build --platform linux/arm64 -f Dockerfile.arm64 -t {{image}} .
    docker create --platform linux/arm64 --name _extract_{{bin}} {{image}}
    docker cp _extract_{{bin}}:/build/{{bin}} /tmp/{{bin}}-arm64
    docker rm _extract_{{bin}}
    @echo "Binary ready: /tmp/{{bin}}-arm64 ($(file /tmp/{{bin}}-arm64 | grep -o 'ARM aarch64'))"

deploy: build
    scp /tmp/{{bin}}-arm64 {{host}}:/tmp/{{bin}}
    ssh {{host}} "sudo install -m 0755 /tmp/{{bin}} /usr/local/bin/{{bin}} && sudo systemctl restart {{bin}}.service"
    @just status

push:
    scp /tmp/{{bin}}-arm64 {{host}}:/tmp/{{bin}}
    ssh {{host}} "sudo install -m 0755 /tmp/{{bin}} /usr/local/bin/{{bin}} && sudo systemctl restart {{bin}}.service"
    @just status

setup: _setup-user _setup-nginx-http _setup-cert _setup-nginx-https _setup-systemd
    @echo "Setup complete. Deploy the binary with: just deploy"

_setup-user:
    ssh {{host}} " \
        sudo useradd --system --home /var/lib/obsidian-sync --shell /usr/sbin/nologin obsidian-sync 2>/dev/null || true; \
        sudo mkdir -p /var/lib/obsidian-sync; \
        sudo chown -R obsidian-sync:obsidian-sync /var/lib/obsidian-sync; \
        sudo mkdir -p /etc/obsidian-sync; \
    "

_setup-nginx-http:
    scp deploy/nginx-http.conf {{host}}:/tmp/{{domain}}.http.conf
    ssh {{host}} " \
        sudo cp /tmp/{{domain}}.http.conf /etc/nginx/sites-available/{{domain}}.http.conf; \
        sudo ln -sf /etc/nginx/sites-available/{{domain}}.http.conf /etc/nginx/sites-enabled/{{domain}}.http.conf; \
        sudo nginx -t && sudo systemctl reload nginx; \
    "

_setup-cert:
    ssh {{host}} "sudo certbot certonly --nginx -d {{domain}} --non-interactive --agree-tos -m {{email}}"

_setup-nginx-https:
    scp deploy/nginx.conf {{host}}:/tmp/{{domain}}.conf
    ssh {{host}} " \
        sudo cp /tmp/{{domain}}.conf /etc/nginx/sites-available/{{domain}}.conf; \
        sudo ln -sf /etc/nginx/sites-available/{{domain}}.conf /etc/nginx/sites-enabled/{{domain}}.conf; \
        sudo rm -f /etc/nginx/sites-enabled/{{domain}}.http.conf; \
        sudo nginx -t && sudo systemctl reload nginx; \
    "

_setup-systemd:
    scp deploy/obsidian-sync-server.service {{host}}:/tmp/
    ssh {{host}} " \
        sudo cp /tmp/obsidian-sync-server.service /etc/systemd/system/obsidian-sync-server.service; \
        if ! sudo test -f /etc/obsidian-sync/env; then \
            sudo sh -c 'echo OBSIDIAN_SYNC_TOKEN=\$\$(openssl rand -hex 32) > /etc/obsidian-sync/env'; \
            sudo chmod 640 /etc/obsidian-sync/env; \
            sudo chown root:obsidian-sync /etc/obsidian-sync/env; \
        fi; \
        sudo systemctl daemon-reload; \
        sudo systemctl enable obsidian-sync-server; \
    "

renew-cert:
    ssh {{host}} "sudo certbot renew --cert-name {{domain}} --force-renewal"
    ssh {{host}} "sudo systemctl reload nginx"

status:
    ssh {{host}} "sudo systemctl status {{bin}}.service --no-pager -l && echo && sudo journalctl -u {{bin}}.service -n 20 --no-pager"

logs:
    ssh {{host}} "sudo journalctl -u {{bin}}.service -f"

token:
    @ssh {{host}} "sudo grep OBSIDIAN_SYNC_TOKEN /etc/obsidian-sync/env"

smoke:
    #!/usr/bin/env bash
    TOKEN=$(ssh {{host}} "sudo grep OBSIDIAN_SYNC_TOKEN /etc/obsidian-sync/env | cut -d= -f2")
    CODE=$(curl -sf -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $TOKEN" https://{{domain}}/manifest)
    echo "$CODE https://{{domain}}/manifest"
    [ "$CODE" = "200" ] && echo "OK" || { echo "FAIL"; exit 1; }
