# Imagen del laboratorio: gateway (Python) + HMI ya compilada.
#   docker compose -f hmi/docker-compose.yml up -d        (gateway + broker MQTT)
# Pensada para un equipo Linux siempre encendido (Raspberry Pi 64 bits, PC) o un servidor.
# Conexión al robot por WiFi o MQTT. Por USB sólo en Linux, pasando el puerto con `devices:`
# (Docker Desktop en macOS/Windows no da acceso a puertos USB).

# ---- 1. HMI: TypeScript → archivos estáticos
FROM node:22-alpine AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY web/ ./
RUN npm run build

# ---- 2. Gateway
FROM python:3.12-slim
WORKDIR /app
COPY gateway/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY gateway/app ./app
COPY --from=web /web/dist ./web/dist

ENV BALANCIN_HOST=0.0.0.0 \
    BALANCIN_PORT=8000 \
    BALANCIN_DB=/data/balancin.db \
    BALANCIN_WEB_DIST=/app/web/dist \
    PYTHONUNBUFFERED=1
VOLUME /data
EXPOSE 8000
HEALTHCHECK --interval=30s --timeout=3s CMD python -c "import urllib.request,sys; urllib.request.urlopen('http://127.0.0.1:8000/api/health', timeout=2)" || exit 1
CMD ["python", "-m", "app"]
