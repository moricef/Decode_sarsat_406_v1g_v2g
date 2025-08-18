# Makefile pour le décodeur 406 MHz COSPAS-SARSAT
# Usage: make, make clean, make install, make debug

# Configuration du compilateur
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS = -lm
DEBUG_FLAGS = -g -DDEBUG -O0

# Fichiers sources et objets
SOURCES = dec406_main.c dec406_v1g.c dec406_v2g.c display_utils.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = dec406
HEADERS = dec406.h display_utils.h

# Règle par défaut
all: $(TARGET)

# Compilation du programme principal
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "✅ Build successful! Run with: ./$(TARGET)"

# Compilation des fichiers objets
%.o: %.c $(HEADERS)
	@echo "🔨 Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Version debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)
	@echo "🐛 Debug version built"

# Version avec décodage avancé
advanced: CFLAGS += -DADVANCED_DECODING
advanced: clean $(TARGET)
	@echo "🚀 Advanced decoding enabled"

# Nettoyage
clean:
	@echo "🧹 Cleaning build files..."
	rm -f $(OBJECTS) $(TARGET) core *.core

# Installation (optionnel)
install: $(TARGET)
	@echo "📦 Installing $(TARGET) to /usr/local/bin/"
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod 755 /usr/local/bin/$(TARGET)

# Désinstallation
uninstall:
	@echo "🗑️  Uninstalling $(TARGET)..."
	sudo rm -f /usr/local/bin/$(TARGET)

# Tests automatisés
test: $(TARGET)
	@echo "🧪 Running tests..."
	./$(TARGET)
	@echo "✅ Tests completed"

# Vérification mémoire avec valgrind
memcheck: debug
	@echo "🔍 Running memory check..."
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# Affichage de l'aide
help:
	@echo "📖 Makefile pour décodeur 406 MHz"
	@echo ""
	@echo "Cibles disponibles:"
	@echo "  all       - Compilation standard (défaut)"
	@echo "  debug     - Compilation avec informations de debug"
	@echo "  advanced  - Active le décodage avancé"
	@echo "  clean     - Nettoie les fichiers de compilation"
	@echo "  install   - Installe le programme dans /usr/local/bin"
	@echo "  uninstall - Désinstalle le programme"
	@echo "  test      - Lance les tests automatisés"
	@echo "  memcheck  - Vérification mémoire avec valgrind"
	@echo "  help      - Affiche cette aide"
	@echo ""
	@echo "Exemples:"
	@echo "  make              # Compilation standard"
	@echo "  make debug        # Version debug"
	@echo "  make clean all    # Nettoyage puis compilation"

# Vérification des dépendances
check-deps:
	@echo "🔍 Checking dependencies..."
	@which gcc > /dev/null || (echo "❌ gcc not found" && exit 1)
	@echo "✅ gcc found: $(shell gcc --version | head -n1)"
	@which valgrind > /dev/null && echo "✅ valgrind available" || echo "⚠️  valgrind not found (optional)"

# Affichage des informations système
info:
	@echo "ℹ️  Build Information:"
	@echo "  CC:      $(CC)"
	@echo "  CFLAGS:  $(CFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  Sources: $(SOURCES)"
	@echo "  Target:  $(TARGET)"
	@echo "  System:  $(shell uname -s)"
	@echo "  Arch:    $(shell uname -m)"

# Sauvegarde du projet
backup:
	@echo "💾 Creating project backup..."
	tar -czf dec406_backup_$(shell date +%Y%m%d_%H%M%S).tar.gz *.c *.h Makefile README*
	@echo "✅ Backup created"

# Archivage pour distribution
dist: clean
	@echo "📦 Creating distribution archive..."
	mkdir -p dec406-dist
	cp *.c *.h Makefile dec406-dist/
	tar -czf dec406-v1.0.tar.gz dec406-dist/
	rm -rf dec406-dist/
	@echo "✅ Distribution archive created: dec406-v1.0.tar.gz"

# Forces rebuild
rebuild: clean all

# Règles phony (pas de fichiers du même nom)
.PHONY: all debug advanced clean install uninstall test memcheck help check-deps info backup dist rebuild

# Couleurs pour le terminal (optionnel)
.SILENT:
