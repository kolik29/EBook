import { create } from "zustand";
import type { Book, BooksStore } from "../types/book";
import { getBooks } from "../api/books";

const normalizeBooks = (books: Book[]): Book[] => {
    if (!books.length) {
        return [];
    }

    const hasActive = books.some((book) => book.active);

    if (hasActive) {
        return books;
    }

    return books.map((book, index) => ({
        ...book,
        active: index === 0,
    }));
};

export const useBooksStore = create<BooksStore>()((set, get) => ({
    books: [],
    loading: false,
    loaded: false,
    query: "",

    setQuery: (query) => set({ query }),

    loadBooks: async () => {
        const { loading, loaded } = get();

        if (loading || loaded) {
            return;
        }

        set({ loading: true });

        try {
            const books = await getBooks();

            set({
                books: normalizeBooks(books),
                loaded: true,
            });
        } catch (error) {
            console.error(error);

            set({
                loaded: false,
            });
        } finally {
            set({ loading: false });
        }
    },

    reloadBooks: async () => {
        if (get().loading) {
            return;
        }

        set({
            loading: true,
            loaded: false,
        });

        try {
            const books = await getBooks();

            set({
                books: normalizeBooks(books),
                loaded: true,
            });
        } catch (error) {
            console.error(error);

            set({
                loaded: false,
            });
        } finally {
            set({ loading: false });
        }
    },

    selectBook: (id) =>
        set((state) => ({
            books: state.books.map((book) => ({
                ...book,
                active: book.id === id,
            })),
        })),
}));