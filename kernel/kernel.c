/* kernel.c
 * Squelette d’un kernel minimal pour un OS en mode protégé
 * Compilation suggérée (avec votre cross-compilateur GCC) :
 *   gcc -m32 -ffreestanding -nostdlib -c kernel.c -o kernel.o
 *
 * Ce code fait :
 *   - L'initialisation de l'écran en mode VGA (écriture directe en mémoire)
 *   - L'initialisation d’une IDT minimale avec un gestionnaire d’interruptions par défaut
 *   - La mise en place d’un paging simple (identité sur 4Mo)
 *   - Une boucle CLI qui affiche un prompt et lit des commandes via le clavier
 */

#include <stdint.h>
#include <stddef.h>

/* --- Gestion de l'affichage en mode VGA --- */

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VIDEO_MEMORY (uint16_t*)0xb8000

/* Couleurs VGA (pour le texte) */
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_WHITE = 15,
};

/* Variables globales pour la gestion de l’affichage */
static uint16_t *vga_buffer = (uint16_t*)0xb8000;
static size_t cursor_x = 0;
static size_t cursor_y = 0;

/* Construit un attribut couleur à partir d'une couleur de premier plan et d’arrière-plan */
static inline uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

/* Construit une entrée VGA (caractère et attribut) */
static inline uint16_t make_vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/* Efface l'écran en remplissant la mémoire vidéo */
void vga_clear_screen() {
    uint8_t color = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint16_t blank = make_vga_entry(' ', color);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = blank;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

/* Affiche un caractère sur l’écran */
void vga_putc(char c) {
    uint8_t color = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0;  // Pour cet exemple, on réinitialise le curseur (bouclage simple)
        }
        return;
    }
    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = make_vga_entry(c, color);
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0;
        }
    }
}

/* Affiche une chaîne de caractères sur l’écran */
void vga_print(const char *str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putc(str[i]);
    }
}

/* --- Initialisation de l’IDT --- */

/* Structure d’une entrée de l’IDT (Interrupt Descriptor Table) */
struct idt_entry {
    uint16_t base_low;   // Bits 0..15 de l'adresse du gestionnaire
    uint16_t sel;        // Sélecteur de segment
    uint8_t  always0;    // Toujours à 0
    uint8_t  flags;      // Type et attributs
    uint16_t base_high;  // Bits 16..31 de l'adresse du gestionnaire
} __attribute__((packed));

/* Structure de pointeur pour charger l’IDT (instruction lidt attend un format particulier) */
struct idt_ptr {
    uint16_t limit;  // Taille de l’IDT - 1
    uint32_t base;   // Adresse de la première entrée de l’IDT
} __attribute__((packed));

#define IDT_ENTRIES 256
struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr   idtp;

/* Fonction qui remplit une entrée de l’IDT
 * @num   : numéro d'entrée
 * @base  : adresse du gestionnaire
 * @sel   : sélecteur de segment (habituellement 0x08 pour le code)
 * @flags : attributs (0x8E pour un interrupteur 32 bits, présent)
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
    idt[num].base_high = (base >> 16) & 0xFFFF;
}

/* Fonction externe (en assembleur) pour charger l’IDT.
 * Voir le fichier idt_flush.S.
 */
extern void idt_flush(uint32_t);

/* Initialisation de l’IDT.
 * Pour ce squelette, toutes les entrées sont pointées vers un gestionnaire par défaut.
 */
void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint32_t)&idt;

    /* Pour ce kernel minimal, nous utilisons un unique gestionnaire d’interruptions
     * par défaut (dummy_handler) pour toutes les entrées.
     */
    extern void dummy_handler();
    for (uint16_t i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, (uint32_t)dummy_handler, 0x08, 0x8E);
    }

    /* Charge l’IDT avec l’instruction lidt */
    idt_flush((uint32_t)&idtp);
}

/* --- Mise en place d’un paging simplifié --- */

#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES     1024

/* Le répertoire de pages et la première table de pages doivent être alignés sur 4K */
uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(4096)));
uint32_t first_page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));

/* Initialisation du paging
 * On réalise ici une identité sur les 4 premiers Mo de mémoire.
 * Chaque entrée est marquée comme présente et en lecture/écriture (flag 0x3).
 */
void init_paging() {
    // Effacer le répertoire de pages
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // Configurer la première table de pages pour une identité sur 4Mo
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        first_page_table[i] = (i * 0x1000) | 3;  // 3 = présent + R/W
    }

    // Le premier entrée du répertoire pointe vers notre table
    page_directory[0] = ((uint32_t)first_page_table) | 3;

    /* Charger le répertoire dans CR3 et activer le paging :
     * - On charge CR3 avec l'adresse de page_directory.
     * - On active le bit PG (bit 31) dans CR0.
     */
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"  // Active le paging (PG = bit 31)
        "mov %%eax, %%cr0\n"
        :
        : "r"(page_directory)
        : "eax"
    );
}

/* --- Gestion du clavier --- */

/* Fonction inline pour lire un octet depuis un port (ici utilisé pour le clavier) */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Table de correspondance scan code → ASCII (layout US, sans gestion des majuscules) */
static const char kbd_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x',
    'c','v','b','n','m',',','.','/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Attend qu'une touche soit pressée et retourne le caractère ASCII correspondant.
 * Cette fonction lit directement depuis le contrôleur du clavier (ports 0x60 et 0x64).
 */
char kbd_getc() {
    uint8_t scancode;
    while (1) {
        // Vérifier si une donnée est disponible sur le port 0x64 (statut)
        if (inb(0x64) & 1) {
            scancode = inb(0x60);
            /* On ne traite que les pressions (scan codes < 128).
             * Si le scan code correspond à un caractère imprimable, on le retourne.
             */
            if (scancode < 128) {
                char ascii = kbd_map[scancode];
                if (ascii)
                    return ascii;
            }
        }
    }
    return 0; // Ne devrait jamais arriver
}

/* Lit une ligne de texte depuis le clavier dans le buffer fourni.
 * La lecture s'arrête dès que le caractère '\n' est reçu ou que le buffer est plein.
 * La fonction retourne le nombre de caractères lus.
 */
size_t read_line(char *buffer, size_t max_length) {
    size_t i = 0;
    while (i < max_length - 1) {
        char c = kbd_getc();
        vga_putc(c);  // Affiche le caractère lu
        if (c == '\n') {
            break;
        }
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return i;
}

/* --- Point d'entrée du kernel --- */

/* kmain() est appelé par le bootloader après passage en mode protégé.
 * Il initialise les sous-systèmes essentiels et lance une boucle CLI.
 */
void kmain(void) {
    vga_clear_screen();

    vga_print("Bienvenue dans le kernel minimal!\n");

    vga_print("Initialisation de l'IDT...\n");
    init_idt();

    vga_print("Configuration du paging...\n");
    init_paging();

    vga_print("Initialisation terminee.\n\n");

    /* Boucle principale du CLI */
    char buffer[256];
    while (1) {
        vga_print("> ");  // Affiche le prompt
        read_line(buffer, sizeof(buffer));
        vga_print("\nCommande recue: ");
        vga_print(buffer);
        vga_print("\n\n");
    }
}

/* --- Gestionnaire d'interruptions par défaut --- */

/* dummy_handler est utilisé pour toutes les entrées de l’IDT.
 * Ici, il sauvegarde les registres, désactive les interruptions et exécute HLT.
 * (Il s'agit d'un handler minimal pour éviter des comportements imprévus.)
 */
__attribute__((naked)) void dummy_handler() {
    __asm__ volatile (
        "pusha\n"     // Sauvegarde tous les registres généraux
        "cli\n"       // Désactive les interruptions
        "hlt\n"       // Arrête le processeur
        "popa\n"      // (Optionnel) Restaure les registres avant de retourner
        "iret\n"      // Retourne de l'interruption
    );
}
