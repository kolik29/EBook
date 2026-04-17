export interface BookPagination {
    total: number;
    current: number;
}

export interface Book {
    id: number;
    title: string;
    author: string;
    img: string;
    active: boolean;
    page: BookPagination
}