# Wir nutzen ein schlankes Python-Image als Basis
FROM python:3.9-slim

# Arbeitsverzeichnis im Container setzen
WORKDIR /app

# Zuerst nur die Requirements kopieren (Caching-Optimierung)
COPY requirements.txt .

# Abhängigkeiten installieren
# --no-cache-dir hält das Image klein
RUN pip install --no-cache-dir -r requirements.txt

# Den Rest des Codes kopieren
COPY . .

# Environment Variable sicherstellen (optional, aber gut für Flask)
ENV PYTHONUNBUFFERED=1

# Startbefehl
CMD ["python", "node.py"]